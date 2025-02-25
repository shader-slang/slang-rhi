#include "metal-device.h"
#include "../resource-desc-utils.h"
#include "metal-command.h"
#include "metal-buffer.h"
#include "metal-shader-program.h"
#include "metal-texture.h"
#include "metal-util.h"
#include "metal-input-layout.h"
#include "metal-fence.h"
#include "metal-query.h"
#include "metal-sampler.h"
#include "metal-shader-object-layout.h"
#include "metal-shader-object.h"
#include "metal-acceleration-structure.h"

#include "core/common.h"

#include <cstdio>
#include <vector>

namespace rhi::metal {

DeviceImpl::DeviceImpl() {}

DeviceImpl::~DeviceImpl()
{
    m_queue.setNull();
}

Result DeviceImpl::getNativeDeviceHandles(DeviceNativeHandles* outHandles)
{
    outHandles->handles[0].type = NativeHandleType::MTLDevice;
    outHandles->handles[0].value = (uint64_t)m_device.get();
    outHandles->handles[1] = {};
    outHandles->handles[2] = {};
    return SLANG_OK;
}

Result DeviceImpl::initialize(const DeviceDesc& desc)
{
    AUTORELEASEPOOL

    // Initialize device info.
    {
        m_info.apiName = "Metal";
        m_info.deviceType = DeviceType::Metal;
        m_info.adapterName = "default";
        static const float kIdentity[] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        ::memcpy(m_info.identityProjectionMatrix, kIdentity, sizeof(kIdentity));
    }

    m_desc = desc;

    SLANG_RETURN_ON_FAIL(Device::initialize(desc));

    m_device = NS::TransferPtr(MTL::CreateSystemDefaultDevice());
    m_commandQueue = NS::TransferPtr(m_device->newCommandQueue(64));
    m_queue = new CommandQueueImpl(this, QueueType::Graphics);
    m_queue->init(m_commandQueue);

    m_hasArgumentBufferTier2 = m_device->argumentBuffersSupport() >= MTL::ArgumentBuffersTier2;

    // Supports surface/swapchain
    m_features.push_back("surface");
    // Supports rasterization
    m_features.push_back("rasterization");

    if (m_hasArgumentBufferTier2)
    {
        m_features.push_back("argument-buffer-tier-2");
        // ParameterBlock requires argument buffer tier 2
        m_features.push_back("parameter-block");
    }

    SLANG_RETURN_ON_FAIL(
        m_slangContext
            .initialize(desc.slang, SLANG_METAL_LIB, "", std::array{slang::PreprocessorMacroDesc{"__METAL__", "1"}})
    );

    // TODO: expose via some other means
    if (captureEnabled())
    {
        MTL::CaptureManager* captureManager = MTL::CaptureManager::sharedCaptureManager();
        MTL::CaptureDescriptor* d = MTL::CaptureDescriptor::alloc()->init();
        if (!captureManager->supportsDestination(MTL::CaptureDestinationGPUTraceDocument))
        {
            printf(
                "Cannot capture MTL calls to document; ensure that Info.plist exists with 'MetalCaptureEnabled' set "
                "to 'true'.\n"
            );
            exit(1);
        }
        d->setDestination(MTL::CaptureDestinationGPUTraceDocument);
        d->setCaptureObject(m_device.get());
        NS::SharedPtr<NS::String> path = MetalUtil::createString("frame.gputrace");
        NS::SharedPtr<NS::URL> url = NS::TransferPtr(NS::URL::alloc()->initFileURLWithPath(path.get()));
        d->setOutputURL(url.get());
        NS::Error* errorCode = NS::Error::alloc();
        if (!captureManager->startCapture(d, &errorCode))
        {
            NS::String* errorString = errorCode->description();
            std::string estr(errorString->cString(NS::UTF8StringEncoding));
            printf("Start capture failure: %s\n", estr.c_str());
            exit(1);
        }
    }
    return SLANG_OK;
}

// void DeviceImpl::waitForGpu() { m_deviceQueue.flushAndWait(); }

const DeviceInfo& DeviceImpl::getDeviceInfo() const
{
    return m_info;
}

Result DeviceImpl::getQueue(QueueType type, ICommandQueue** outQueue)
{
    AUTORELEASEPOOL

    if (type != QueueType::Graphics)
    {
        return SLANG_FAIL;
    }
    m_queue->establishStrongReferenceToDevice();
    returnComPtr(outQueue, m_queue);
    return SLANG_OK;
}

Result DeviceImpl::readTexture(ITexture* texture, ISlangBlob** outBlob, Size* outRowPitch, Size* outPixelSize)
{
    AUTORELEASEPOOL

    TextureImpl* textureImpl = checked_cast<TextureImpl*>(texture);

    if (textureImpl->m_desc.sampleCount > 1)
    {
        return SLANG_E_NOT_IMPLEMENTED;
    }

    NS::SharedPtr<MTL::Texture> srcTexture = textureImpl->m_texture;

    const TextureDesc& desc = textureImpl->m_desc;
    uint32_t width = max(desc.size.width, 1);
    uint32_t height = max(desc.size.height, 1);
    uint32_t depth = max(desc.size.depth, 1);
    const FormatInfo& formatInfo = getFormatInfo(desc.format);
    Size bytesPerPixel = formatInfo.blockSizeInBytes / formatInfo.pixelsPerBlock;
    Size bytesPerRow = Size(width) * bytesPerPixel;
    Size bytesPerSlice = Size(height) * bytesPerRow;
    Size bufferSize = Size(depth) * bytesPerSlice;
    if (outRowPitch)
        *outRowPitch = bytesPerRow;
    if (outPixelSize)
        *outPixelSize = bytesPerPixel;

    // create staging buffer
    NS::SharedPtr<MTL::Buffer> stagingBuffer = NS::TransferPtr(m_device->newBuffer(bufferSize, MTL::StorageModeShared));
    if (!stagingBuffer)
    {
        return SLANG_FAIL;
    }

    MTL::CommandBuffer* commandBuffer = m_commandQueue->commandBuffer();
    MTL::BlitCommandEncoder* encoder = commandBuffer->blitCommandEncoder();
    encoder->copyFromTexture(
        srcTexture.get(),
        0,
        0,
        MTL::Origin(0, 0, 0),
        MTL::Size(width, height, depth),
        stagingBuffer.get(),
        0,
        bytesPerRow,
        bytesPerSlice
    );
    encoder->endEncoding();
    commandBuffer->commit();
    commandBuffer->waitUntilCompleted();

    auto blob = OwnedBlob::create(bufferSize);
    ::memcpy((void*)blob->getBufferPointer(), stagingBuffer->contents(), bufferSize);

    returnComPtr(outBlob, blob);
    return SLANG_OK;
}

Result DeviceImpl::readBuffer(IBuffer* buffer, Offset offset, Size size, ISlangBlob** outBlob)
{
    AUTORELEASEPOOL

    // create staging buffer
    NS::SharedPtr<MTL::Buffer> stagingBuffer = NS::TransferPtr(m_device->newBuffer(size, MTL::StorageModeShared));
    if (!stagingBuffer)
    {
        return SLANG_FAIL;
    }

    MTL::CommandBuffer* commandBuffer = m_commandQueue->commandBuffer();
    MTL::BlitCommandEncoder* blitEncoder = commandBuffer->blitCommandEncoder();
    blitEncoder
        ->copyFromBuffer(checked_cast<BufferImpl*>(buffer)->m_buffer.get(), offset, stagingBuffer.get(), 0, size);
    blitEncoder->endEncoding();
    commandBuffer->commit();
    commandBuffer->waitUntilCompleted();

    auto blob = OwnedBlob::create(size);
    ::memcpy((void*)blob->getBufferPointer(), stagingBuffer->contents(), size);

    returnComPtr(outBlob, blob);
    return SLANG_OK;
}

Result DeviceImpl::getAccelerationStructureSizes(
    const AccelerationStructureBuildDesc& desc,
    AccelerationStructureSizes* outSizes
)
{
    AUTORELEASEPOOL

    AccelerationStructureDescBuilder builder;
    builder.build(desc, nullptr, m_debugCallback);
    MTL::AccelerationStructureSizes sizes = m_device->accelerationStructureSizes(builder.descriptor.get());
    outSizes->accelerationStructureSize = sizes.accelerationStructureSize;
    outSizes->scratchSize = sizes.buildScratchBufferSize;
    outSizes->updateScratchSize = sizes.refitScratchBufferSize;

    return SLANG_OK;
}

NS::Array* DeviceImpl::getAccelerationStructureArray()
{
    if (m_accelerationStructures.dirty)
    {
        m_accelerationStructures.array = NS::TransferPtr(NS::Array::alloc()->init(
            (const NS::Object* const*)m_accelerationStructures.list.data(),
            m_accelerationStructures.list.size()
        ));
        m_accelerationStructures.dirty = false;
    }
    return m_accelerationStructures.array.get();
}

Result DeviceImpl::getTextureAllocationInfo(const TextureDesc& descIn, Size* outSize, Size* outAlignment)
{
    AUTORELEASEPOOL

    auto alignTo = [&](Size size, Size alignment) -> Size { return ((size + alignment - 1) / alignment) * alignment; };

    TextureDesc desc = fixupTextureDesc(descIn);
    const FormatInfo& formatInfo = getFormatInfo(desc.format);
    MTL::PixelFormat pixelFormat = MetalUtil::translatePixelFormat(desc.format);
    Size alignment = m_device->minimumLinearTextureAlignmentForPixelFormat(pixelFormat);
    Size size = 0;
    Extents extents = desc.size;
    extents.width = extents.width ? extents.width : 1;
    extents.height = extents.height ? extents.height : 1;
    extents.depth = extents.depth ? extents.depth : 1;

    for (uint32_t i = 0; i < desc.mipLevelCount; ++i)
    {
        Size rowSize =
            ((extents.width + formatInfo.blockWidth - 1) / formatInfo.blockWidth) * formatInfo.blockSizeInBytes;
        rowSize = alignTo(rowSize, alignment);
        Size sliceSize = rowSize * alignTo(extents.height, formatInfo.blockHeight);
        size += sliceSize * extents.depth;
        extents.width = max(1, extents.width / 2);
        extents.height = max(1, extents.height / 2);
        extents.depth = max(1, extents.depth / 2);
    }
    size *= desc.arrayLength * (desc.type == TextureType::TextureCube ? 6 : 1);

    *outSize = size;
    *outAlignment = alignment;

    return SLANG_OK;
}

Result DeviceImpl::getTextureRowAlignment(Size* outAlignment)
{
    AUTORELEASEPOOL

    *outAlignment = 1;
    return SLANG_E_NOT_IMPLEMENTED;
}

Result DeviceImpl::getFormatSupport(Format format, FormatSupport* outFormatSupport)
{
    AUTORELEASEPOOL

    // TODO - add table based on https://developer.apple.com/metal/Metal-Feature-Set-Tables.pdf
    FormatSupport support = FormatSupport::None;
    support |= FormatSupport::Buffer;
    support |= FormatSupport::IndexBuffer;
    support |= FormatSupport::VertexBuffer;
    support |= FormatSupport::Texture;
    support |= FormatSupport::DepthStencil;
    support |= FormatSupport::RenderTarget;
    support |= FormatSupport::Blendable;
    support |= FormatSupport::ShaderLoad;
    support |= FormatSupport::ShaderSample;
    support |= FormatSupport::ShaderUavLoad;
    support |= FormatSupport::ShaderUavStore;
    support |= FormatSupport::ShaderAtomic;

    *outFormatSupport = support;
    return SLANG_OK;
}

Result DeviceImpl::createShaderProgram(
    const ShaderProgramDesc& desc,
    IShaderProgram** outProgram,
    ISlangBlob** outDiagnosticBlob
)
{
    AUTORELEASEPOOL

    RefPtr<ShaderProgramImpl> shaderProgram = new ShaderProgramImpl(this);
    shaderProgram->init(desc);
    SLANG_RETURN_ON_FAIL(RootShaderObjectLayoutImpl::create(
        this,
        shaderProgram->linkedProgram,
        shaderProgram->linkedProgram->getLayout(),
        shaderProgram->m_rootObjectLayout.writeRef()
    ));
    returnComPtr(outProgram, shaderProgram);
    return SLANG_OK;
}

Result DeviceImpl::createShaderObjectLayout(
    slang::ISession* session,
    slang::TypeLayoutReflection* typeLayout,
    ShaderObjectLayout** outLayout
)
{
    AUTORELEASEPOOL

    RefPtr<ShaderObjectLayoutImpl> layout;
    SLANG_RETURN_ON_FAIL(ShaderObjectLayoutImpl::createForElementType(this, session, typeLayout, layout.writeRef()));
    returnRefPtrMove(outLayout, layout);
    return SLANG_OK;
}

Result DeviceImpl::createRootShaderObjectLayout(
    slang::IComponentType* program,
    slang::ProgramLayout* programLayout,
    ShaderObjectLayout** outLayout
)
{
    return SLANG_FAIL;
}

Result DeviceImpl::createShaderTable(const ShaderTableDesc& desc, IShaderTable** outShaderTable)
{
    AUTORELEASEPOOL

    return SLANG_E_NOT_IMPLEMENTED;
}

Result DeviceImpl::createQueryPool(const QueryPoolDesc& desc, IQueryPool** outPool)
{
    AUTORELEASEPOOL

    RefPtr<QueryPoolImpl> poolImpl = new QueryPoolImpl();
    SLANG_RETURN_ON_FAIL(poolImpl->init(this, desc));
    returnComPtr(outPool, poolImpl);
    return SLANG_OK;
}

} // namespace rhi::metal
