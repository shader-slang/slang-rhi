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
    if (captureEnabled())
    {
        MTL::CaptureManager* captureManager = MTL::CaptureManager::sharedCaptureManager();
        captureManager->stopCapture();
    }

    m_queue.setNull();
    m_clearEngine.release();
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

    SLANG_RETURN_ON_FAIL(Device::initialize(desc));

    m_device = NS::TransferPtr(MTL::CreateSystemDefaultDevice());
    if (!m_device)
    {
        return SLANG_FAIL;
    }
    m_commandQueue = NS::TransferPtr(m_device->newCommandQueue(64));
    if (!m_commandQueue)
    {
        return SLANG_FAIL;
    }
    m_queue = new CommandQueueImpl(this, QueueType::Graphics);
    m_queue->init(m_commandQueue);

    // Setup capture manager.
    if (captureEnabled())
    {
        MTL::CaptureManager* captureManager = MTL::CaptureManager::sharedCaptureManager();
        MTL::CaptureDescriptor* d = MTL::CaptureDescriptor::alloc()->init();
        if (!captureManager->supportsDestination(MTL::CaptureDestinationGPUTraceDocument))
        {
            handleMessage(
                DebugMessageType::Error,
                DebugMessageSource::Layer,
                "Cannot capture MTL calls to document; ensure that Info.plist exists with 'MetalCaptureEnabled' set to "
                "'true'."
            );
            return SLANG_FAIL;
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
            std::string str(errorString->cString(NS::UTF8StringEncoding));
            str = "Start capture failure: " + str;
            handleMessage(DebugMessageType::Error, DebugMessageSource::Layer, str.c_str());
            return SLANG_FAIL;
        }
    }

    // Initialize device info.
    {
        m_info.deviceType = DeviceType::Metal;
        m_info.apiName = "Metal";
        m_info.adapterName = "default";
        m_info.adapterLUID = {};
    }

    // Initialize features & capabilities.

    addFeature(Feature::HardwareDevice);
    addFeature(Feature::Surface);
    addFeature(Feature::Rasterization);

    if (m_device->supportsRaytracing())
    {
        addFeature(Feature::AccelerationStructure);
    }

    m_hasArgumentBufferTier2 = m_device->argumentBuffersSupport() >= MTL::ArgumentBuffersTier2;
    if (m_hasArgumentBufferTier2)
    {
        addFeature(Feature::ArgumentBufferTier2);
        addFeature(Feature::ParameterBlock);
    }

    addCapability(Capability::metal);

    // Initialize format support table.
    // TODO: add table based on https://developer.apple.com/metal/Metal-Feature-Set-Tables.pdf
    for (size_t formatIndex = 0; formatIndex < size_t(Format::_Count); ++formatIndex)
    {
        Format format = Format(formatIndex);
        FormatSupport formatSupport = FormatSupport::None;
        if (MetalUtil::translatePixelFormat(format) != MTL::PixelFormatInvalid)
        {
            // depth/stencil formats?
            formatSupport |= FormatSupport::CopySource;
            formatSupport |= FormatSupport::CopyDestination;
            formatSupport |= FormatSupport::Texture;
            if (isDepthFormat(format))
                formatSupport |= FormatSupport::DepthStencil;
            formatSupport |= FormatSupport::RenderTarget;
            formatSupport |= FormatSupport::Blendable;
            formatSupport |= FormatSupport::Resolvable;
            formatSupport |= FormatSupport::ShaderLoad;
            formatSupport |= FormatSupport::ShaderSample;
            formatSupport |= FormatSupport::ShaderUavLoad;
            formatSupport |= FormatSupport::ShaderUavStore;
            formatSupport |= FormatSupport::ShaderAtomic;
            formatSupport |= FormatSupport::Buffer;
        }
        if (MetalUtil::translateVertexFormat(format) != MTL::VertexFormatInvalid)
        {
            formatSupport |= FormatSupport::VertexBuffer;
            formatSupport |= FormatSupport::CopySource;
            formatSupport |= FormatSupport::CopyDestination;
        }
        if (format == Format::R32Uint || format == Format::R16Uint)
        {
            formatSupport |= FormatSupport::IndexBuffer;
            formatSupport |= FormatSupport::CopySource;
            formatSupport |= FormatSupport::CopyDestination;
        }
        m_formatSupport[formatIndex] = formatSupport;
    }

    // Initialize slang context.
    SLANG_RETURN_ON_FAIL(
        m_slangContext
            .initialize(desc.slang, SLANG_METAL_LIB, "", std::array{slang::PreprocessorMacroDesc{"__METAL__", "1"}})
    );

    SLANG_RETURN_ON_FAIL(m_clearEngine.initialize(m_device.get()));

    return SLANG_OK;
}

// void DeviceImpl::waitForGpu() { m_deviceQueue.flushAndWait(); }

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

Result DeviceImpl::readBuffer(IBuffer* buffer, Offset offset, Size size, void* outData)
{
    AUTORELEASEPOOL

    auto bufferImpl = checked_cast<BufferImpl*>(buffer);
    if (offset + size > bufferImpl->m_desc.size)
    {
        return SLANG_FAIL;
    }

    // create staging buffer
    NS::SharedPtr<MTL::Buffer> stagingBuffer =
        NS::TransferPtr(m_device->newBuffer(size, MTL::ResourceStorageModeManaged));
    if (!stagingBuffer)
    {
        return SLANG_FAIL;
    }

    MTL::CommandBuffer* commandBuffer = m_commandQueue->commandBuffer();
    MTL::BlitCommandEncoder* blitEncoder = commandBuffer->blitCommandEncoder();
    blitEncoder->copyFromBuffer(bufferImpl->m_buffer.get(), offset, stagingBuffer.get(), 0, size);
    blitEncoder->synchronizeResource(stagingBuffer.get());
    blitEncoder->endEncoding();
    commandBuffer->commit();
    commandBuffer->waitUntilCompleted();

    std::memcpy(outData, stagingBuffer->contents(), size);

    return SLANG_OK;
}

Result DeviceImpl::getAccelerationStructureSizes(
    const AccelerationStructureBuildDesc& desc,
    AccelerationStructureSizes* outSizes
)
{
    AUTORELEASEPOOL

    AccelerationStructureBuildDescConverter converter;
    SLANG_RETURN_ON_FAIL(converter.convert(desc, nullptr, m_debugCallback));
    MTL::AccelerationStructureSizes sizes = m_device->accelerationStructureSizes(converter.descriptor.get());
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

Result DeviceImpl::getTextureAllocationInfo(const TextureDesc& desc_, Size* outSize, Size* outAlignment)
{
    AUTORELEASEPOOL

    auto alignTo = [&](Size size, Size alignment) -> Size { return ((size + alignment - 1) / alignment) * alignment; };

    TextureDesc desc = fixupTextureDesc(desc_);
    const FormatInfo& formatInfo = getFormatInfo(desc.format);
    MTL::PixelFormat pixelFormat = MetalUtil::translatePixelFormat(desc.format);
    Size alignment = m_device->minimumLinearTextureAlignmentForPixelFormat(pixelFormat);
    Size size = 0;
    Extent3D extent = desc.size;

    for (uint32_t i = 0; i < desc.mipCount; ++i)
    {
        Size rowSize =
            ((extent.width + formatInfo.blockWidth - 1) / formatInfo.blockWidth) * formatInfo.blockSizeInBytes;
        rowSize = alignTo(rowSize, alignment);
        Size sliceSize = rowSize * alignTo(extent.height, formatInfo.blockHeight);
        size += sliceSize * extent.depth;
        extent.width = max(1u, extent.width >> 1);
        extent.height = max(1u, extent.height >> 1);
        extent.depth = max(1u, extent.depth >> 1);
    }
    size *= desc.getLayerCount();

    *outSize = size;
    *outAlignment = alignment;

    return SLANG_OK;
}

Result DeviceImpl::getTextureRowAlignment(Format format, Size* outAlignment)
{
    AUTORELEASEPOOL
    if (format == Format::Undefined)
        return SLANG_FAIL;
    const FormatInfo& formatInfo = getFormatInfo(format);
    if (formatInfo.isCompressed)
    {
        *outAlignment = formatInfo.blockSizeInBytes;
    }
    else
    {
        MTL::PixelFormat pixelFormat = MetalUtil::translatePixelFormat(format);
        *outAlignment = m_device->minimumLinearTextureAlignmentForPixelFormat(pixelFormat);
    }
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

    RefPtr<QueryPoolImpl> poolImpl = new QueryPoolImpl(this, desc);
    SLANG_RETURN_ON_FAIL(poolImpl->init());
    returnComPtr(outPool, poolImpl);
    return SLANG_OK;
}

} // namespace rhi::metal
