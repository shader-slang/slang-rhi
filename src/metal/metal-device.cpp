#include "metal-device.h"
#include "../resource-desc-utils.h"
#include "metal-buffer.h"
#include "metal-shader-program.h"
#include "metal-texture.h"
#include "metal-util.h"
#include "metal-vertex-layout.h"
// #include "metal-command-queue.h"
#include "metal-fence.h"
#include "metal-query.h"
#include "metal-sampler.h"
#include "metal-shader-object-layout.h"
#include "metal-shader-object.h"
// #include "metal-shader-table.h"
#include "metal-transient-heap.h"
// #include "metal-pipeline-dump-layer.h"
// #include "metal-helper-functions.h"
#include "metal-acceleration-structure.h"

#include "core/common.h"

#include <cstdio>
#include <vector>

namespace rhi::metal {

DeviceImpl::~DeviceImpl()
{
    m_queue.setNull();
}

Result DeviceImpl::getNativeDeviceHandles(NativeHandles* outHandles)
{
    outHandles->handles[0].type = NativeHandleType::MTLDevice;
    outHandles->handles[0].value = (uint64_t)m_device.get();
    outHandles->handles[1] = {};
    outHandles->handles[2] = {};
    return SLANG_OK;
}

Result DeviceImpl::initialize(const Desc& desc)
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
    Result initDeviceResult = SLANG_OK;

    m_device = NS::TransferPtr(MTL::CreateSystemDefaultDevice());
    m_commandQueue = NS::TransferPtr(m_device->newCommandQueue(64));
    m_queue = new CommandQueueImpl(this, QueueType::Graphics);
    m_queue->init(m_commandQueue);

    m_hasArgumentBufferTier2 = m_device->argumentBuffersSupport() >= MTL::ArgumentBuffersTier2;

    if (m_hasArgumentBufferTier2)
    {
        m_features.push_back("argument-buffer-tier-2");
    }

    SLANG_RETURN_ON_FAIL(slangContext.initialize(
        desc.slang,
        desc.extendedDescCount,
        desc.extendedDescs,
        SLANG_METAL_LIB,
        "",
        std::array{slang::PreprocessorMacroDesc{"__METAL__", "1"}}
    ));

    // TODO: expose via some other means
    if (captureEnabled())
    {
        MTL::CaptureManager* captureManager = MTL::CaptureManager::sharedCaptureManager();
        MTL::CaptureDescriptor* d = MTL::CaptureDescriptor::alloc()->init();
        MTL::CaptureDestination captureDest = MTL::CaptureDestination::CaptureDestinationGPUTraceDocument;
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

Result DeviceImpl::createTransientResourceHeap(
    const ITransientResourceHeap::Desc& desc,
    ITransientResourceHeap** outHeap
)
{
    AUTORELEASEPOOL

    RefPtr<TransientResourceHeapImpl> result = new TransientResourceHeapImpl();
    SLANG_RETURN_ON_FAIL(result->init(desc, this));
    returnComPtr(outHeap, result);
    return SLANG_OK;
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

    TextureImpl* textureImpl = static_cast<TextureImpl*>(texture);

    if (textureImpl->m_desc.sampleCount > 1)
    {
        return SLANG_E_NOT_IMPLEMENTED;
    }

    NS::SharedPtr<MTL::Texture> srcTexture = textureImpl->m_texture;

    const TextureDesc& desc = textureImpl->m_desc;
    GfxCount width = std::max(desc.size.width, 1);
    GfxCount height = std::max(desc.size.height, 1);
    GfxCount depth = std::max(desc.size.depth, 1);
    FormatInfo formatInfo;
    rhiGetFormatInfo(desc.format, &formatInfo);
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
    blitEncoder->copyFromBuffer(static_cast<BufferImpl*>(buffer)->m_buffer.get(), offset, stagingBuffer.get(), 0, size);
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
    builder.build(desc, nullptr, getDebugCallback());
    MTL::AccelerationStructureSizes sizes = m_device->accelerationStructureSizes(builder.descriptor.get());
    outSizes->accelerationStructureSize = sizes.accelerationStructureSize;
    outSizes->scratchSize = sizes.buildScratchBufferSize;
    outSizes->updateScratchSize = sizes.refitScratchBufferSize;

    return SLANG_OK;
}

Result DeviceImpl::createAccelerationStructure(
    const AccelerationStructureDesc& desc,
    IAccelerationStructure** outAccelerationStructure
)
{
    AUTORELEASEPOOL

    RefPtr<AccelerationStructureImpl> result = new AccelerationStructureImpl(this, desc);
    result->m_accelerationStructure = NS::TransferPtr(m_device->newAccelerationStructure(desc.size));

    uint32_t globalIndex = 0;
    if (!m_accelerationStructures.freeList.empty())
    {
        globalIndex = m_accelerationStructures.freeList.back();
        m_accelerationStructures.freeList.pop_back();
        m_accelerationStructures.list[globalIndex] = result->m_accelerationStructure.get();
    }
    else
    {
        globalIndex = m_accelerationStructures.list.size();
        m_accelerationStructures.list.push_back(result->m_accelerationStructure.get());
    }
    m_accelerationStructures.dirty = true;
    result->m_globalIndex = globalIndex;

    returnComPtr(outAccelerationStructure, result);
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
    FormatInfo formatInfo;
    rhiGetFormatInfo(desc.format, &formatInfo);
    MTL::PixelFormat pixelFormat = MetalUtil::translatePixelFormat(desc.format);
    Size alignment = m_device->minimumLinearTextureAlignmentForPixelFormat(pixelFormat);
    Size size = 0;
    Extents extents = desc.size;
    extents.width = extents.width ? extents.width : 1;
    extents.height = extents.height ? extents.height : 1;
    extents.depth = extents.depth ? extents.depth : 1;

    for (Int i = 0; i < desc.mipLevelCount; ++i)
    {
        Size rowSize =
            ((extents.width + formatInfo.blockWidth - 1) / formatInfo.blockWidth) * formatInfo.blockSizeInBytes;
        rowSize = alignTo(rowSize, alignment);
        Size sliceSize = rowSize * alignTo(extents.height, formatInfo.blockHeight);
        size += sliceSize * extents.depth;
        extents.width = std::max(1, extents.width / 2);
        extents.height = std::max(1, extents.height / 2);
        extents.depth = std::max(1, extents.depth / 2);
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

Result DeviceImpl::createTexture(const TextureDesc& descIn, const SubresourceData* initData, ITexture** outTexture)
{
    AUTORELEASEPOOL

    TextureDesc desc = fixupTextureDesc(descIn);

    // Metal doesn't support mip-mapping for 1D textures
    // However, we still need to use the provided mip level count when initializing the texture
    GfxCount initMipLevels = desc.mipLevelCount;
    desc.mipLevelCount = desc.type == TextureType::Texture1D ? 1 : desc.mipLevelCount;

    const MTL::PixelFormat pixelFormat = MetalUtil::translatePixelFormat(desc.format);
    if (pixelFormat == MTL::PixelFormat::PixelFormatInvalid)
    {
        SLANG_RHI_ASSERT_FAILURE("Unsupported texture format");
        return SLANG_FAIL;
    }

    RefPtr<TextureImpl> textureImpl(new TextureImpl(desc));

    NS::SharedPtr<MTL::TextureDescriptor> textureDesc = NS::TransferPtr(MTL::TextureDescriptor::alloc()->init());
    switch (desc.memoryType)
    {
    case MemoryType::DeviceLocal:
        textureDesc->setStorageMode(MTL::StorageModePrivate);
        break;
    case MemoryType::Upload:
        textureDesc->setStorageMode(MTL::StorageModeShared);
        textureDesc->setCpuCacheMode(MTL::CPUCacheModeWriteCombined);
        break;
    case MemoryType::ReadBack:
        textureDesc->setStorageMode(MTL::StorageModeShared);
        break;
    }

    bool isArray = desc.arrayLength > 1;

    switch (desc.type)
    {
    case TextureType::Texture1D:
        textureDesc->setTextureType(isArray ? MTL::TextureType1DArray : MTL::TextureType1D);
        textureDesc->setWidth(desc.size.width);
        break;
    case TextureType::Texture2D:
        if (desc.sampleCount > 1)
        {
            textureDesc->setTextureType(isArray ? MTL::TextureType2DMultisampleArray : MTL::TextureType2DMultisample);
            textureDesc->setSampleCount(desc.sampleCount);
        }
        else
        {
            textureDesc->setTextureType(isArray ? MTL::TextureType2DArray : MTL::TextureType2D);
        }
        textureDesc->setWidth(descIn.size.width);
        textureDesc->setHeight(descIn.size.height);
        break;
    case TextureType::TextureCube:
        textureDesc->setTextureType(isArray ? MTL::TextureTypeCubeArray : MTL::TextureTypeCube);
        textureDesc->setWidth(descIn.size.width);
        textureDesc->setHeight(descIn.size.height);
        break;
    case TextureType::Texture3D:
        textureDesc->setTextureType(MTL::TextureType::TextureType3D);
        textureDesc->setWidth(descIn.size.width);
        textureDesc->setHeight(descIn.size.height);
        textureDesc->setDepth(descIn.size.depth);
        break;
    default:
        SLANG_RHI_ASSERT_FAILURE("Unsupported texture type");
        return SLANG_FAIL;
    }

    MTL::TextureUsage textureUsage = MTL::TextureUsageUnknown;
    if (is_set(desc.usage, TextureUsage::RenderTarget))
    {
        textureUsage |= MTL::TextureUsageRenderTarget;
    }
    if (is_set(desc.usage, TextureUsage::ShaderResource))
    {
        textureUsage |= MTL::TextureUsageShaderRead;
    }
    if (is_set(desc.usage, TextureUsage::UnorderedAccess))
    {
        textureUsage |= MTL::TextureUsageShaderRead;
        textureUsage |= MTL::TextureUsageShaderWrite;

        // Request atomic access if the format allows it.
        switch (desc.format)
        {
        case Format::R32_UINT:
        case Format::R32_SINT:
        case Format::R32G32_UINT:
        case Format::R32G32_SINT:
            textureUsage |= MTL::TextureUsageShaderAtomic;
            break;
        }
    }

    textureDesc->setMipmapLevelCount(desc.mipLevelCount);
    textureDesc->setArrayLength(desc.arrayLength);
    textureDesc->setPixelFormat(pixelFormat);
    textureDesc->setUsage(textureUsage);
    textureDesc->setSampleCount(desc.sampleCount);
    textureDesc->setAllowGPUOptimizedContents(desc.memoryType == MemoryType::DeviceLocal);

    textureImpl->m_texture = NS::TransferPtr(m_device->newTexture(textureDesc.get()));
    if (!textureImpl->m_texture)
    {
        return SLANG_FAIL;
    }
    textureImpl->m_textureType = textureDesc->textureType();
    textureImpl->m_pixelFormat = textureDesc->pixelFormat();

    if (desc.label)
    {
        textureImpl->m_texture->setLabel(MetalUtil::createString(desc.label).get());
    }

    // TODO: handle initData
    if (initData)
    {
        textureDesc->setStorageMode(MTL::StorageModeManaged);
        textureDesc->setCpuCacheMode(MTL::CPUCacheModeDefaultCache);
        NS::SharedPtr<MTL::Texture> stagingTexture = NS::TransferPtr(m_device->newTexture(textureDesc.get()));

        MTL::CommandBuffer* commandBuffer = m_commandQueue->commandBuffer();
        MTL::BlitCommandEncoder* encoder = commandBuffer->blitCommandEncoder();
        if (!stagingTexture || !commandBuffer || !encoder)
        {
            return SLANG_FAIL;
        }

        GfxCount sliceCount = desc.arrayLength * (desc.type == TextureType::TextureCube ? 6 : 1);

        for (Index slice = 0; slice < sliceCount; ++slice)
        {
            MTL::Region region;
            region.origin = MTL::Origin(0, 0, 0);
            region.size = MTL::Size(desc.size.width, desc.size.height, desc.size.depth);
            for (Index level = 0; level < initMipLevels; ++level)
            {
                if (level >= desc.mipLevelCount)
                    continue;
                const SubresourceData& subresourceData = initData[slice * initMipLevels + level];
                stagingTexture->replaceRegion(
                    region,
                    level,
                    slice,
                    subresourceData.data,
                    subresourceData.strideY,
                    subresourceData.strideZ
                );
                encoder->synchronizeTexture(stagingTexture.get(), slice, level);
                region.size.width = region.size.width > 0 ? std::max(1ul, region.size.width >> 1) : 0;
                region.size.height = region.size.height > 0 ? std::max(1ul, region.size.height >> 1) : 0;
                region.size.depth = region.size.depth > 0 ? std::max(1ul, region.size.depth >> 1) : 0;
            }
        }

        encoder->copyFromTexture(stagingTexture.get(), textureImpl->m_texture.get());
        encoder->endEncoding();
        commandBuffer->commit();
        commandBuffer->waitUntilCompleted();
    }

    returnComPtr(outTexture, textureImpl);
    return SLANG_OK;
}

Result DeviceImpl::createBuffer(const BufferDesc& descIn, const void* initData, IBuffer** outBuffer)
{
    AUTORELEASEPOOL

    BufferDesc desc = fixupBufferDesc(descIn);

    const Size bufferSize = desc.size;

    MTL::ResourceOptions resourceOptions = MTL::ResourceOptions(0);
    switch (desc.memoryType)
    {
    case MemoryType::DeviceLocal:
        resourceOptions = MTL::ResourceStorageModePrivate;
        break;
    case MemoryType::Upload:
        resourceOptions = MTL::ResourceStorageModeShared | MTL::CPUCacheModeWriteCombined;
        break;
    case MemoryType::ReadBack:
        resourceOptions = MTL::ResourceStorageModeShared;
        break;
    }
    resourceOptions |=
        (desc.memoryType == MemoryType::DeviceLocal) ? MTL::ResourceStorageModePrivate : MTL::ResourceStorageModeShared;

    RefPtr<BufferImpl> buffer(new BufferImpl(desc));
    buffer->m_buffer = NS::TransferPtr(m_device->newBuffer(bufferSize, resourceOptions));
    if (!buffer->m_buffer)
    {
        return SLANG_FAIL;
    }

    if (desc.label)
        buffer->m_buffer->addDebugMarker(MetalUtil::createString(desc.label).get(), NS::Range(0, desc.size));

    if (initData)
    {
        NS::SharedPtr<MTL::Buffer> stagingBuffer = NS::TransferPtr(
            m_device->newBuffer(initData, bufferSize, MTL::ResourceStorageModeShared | MTL::CPUCacheModeWriteCombined)
        );
        MTL::CommandBuffer* commandBuffer = m_commandQueue->commandBuffer();
        MTL::BlitCommandEncoder* encoder = commandBuffer->blitCommandEncoder();
        if (!stagingBuffer || !commandBuffer || !encoder)
        {
            return SLANG_FAIL;
        }
        encoder->copyFromBuffer(stagingBuffer.get(), 0, buffer->m_buffer.get(), 0, bufferSize);
        encoder->endEncoding();
        commandBuffer->commit();
        commandBuffer->waitUntilCompleted();
    }

    returnComPtr(outBuffer, buffer);
    return SLANG_OK;
}

Result DeviceImpl::createBufferFromNativeHandle(NativeHandle handle, const BufferDesc& srcDesc, IBuffer** outBuffer)
{
    AUTORELEASEPOOL

    return SLANG_E_NOT_IMPLEMENTED;
}

Result DeviceImpl::createSampler(SamplerDesc const& desc, ISampler** outSampler)
{
    AUTORELEASEPOOL

    RefPtr<SamplerImpl> samplerImpl = new SamplerImpl(desc);
    SLANG_RETURN_ON_FAIL(samplerImpl->init(this, desc));
    returnComPtr(outSampler, samplerImpl);
    return SLANG_OK;
}

Result DeviceImpl::createTextureView(ITexture* texture, const TextureViewDesc& desc, ITextureView** outView)
{
    AUTORELEASEPOOL

    auto textureImpl = static_cast<TextureImpl*>(texture);
    RefPtr<TextureViewImpl> viewImpl = new TextureViewImpl(desc);
    viewImpl->m_texture = textureImpl;
    if (viewImpl->m_desc.format == Format::Unknown)
        viewImpl->m_desc.format = viewImpl->m_texture->m_desc.format;
    viewImpl->m_desc.subresourceRange = viewImpl->m_texture->resolveSubresourceRange(desc.subresourceRange);

    const TextureDesc& textureDesc = textureImpl->m_desc;
    GfxCount layerCount = textureDesc.arrayLength * (textureDesc.type == TextureType::TextureCube ? 6 : 1);
    SubresourceRange sr = viewImpl->m_desc.subresourceRange;
    if (sr.mipLevel == 0 && sr.mipLevelCount == textureDesc.mipLevelCount && sr.baseArrayLayer == 0 &&
        sr.layerCount == layerCount)
    {
        viewImpl->m_textureView = textureImpl->m_texture;
        returnComPtr(outView, viewImpl);
        return SLANG_OK;
    }

    MTL::PixelFormat pixelFormat =
        desc.format == Format::Unknown ? textureImpl->m_pixelFormat : MetalUtil::translatePixelFormat(desc.format);
    NS::Range levelRange(sr.baseArrayLayer, sr.layerCount);
    NS::Range sliceRange(sr.mipLevel, sr.mipLevelCount);

    viewImpl->m_textureView = NS::TransferPtr(
        textureImpl->m_texture->newTextureView(pixelFormat, textureImpl->m_textureType, levelRange, sliceRange)
    );
    if (!viewImpl->m_textureView)
    {
        return SLANG_FAIL;
    }

    returnComPtr(outView, viewImpl);
    return SLANG_OK;
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

Result DeviceImpl::createInputLayout(InputLayoutDesc const& desc, IInputLayout** outLayout)
{
    AUTORELEASEPOOL

    RefPtr<InputLayoutImpl> layoutImpl(new InputLayoutImpl);
    SLANG_RETURN_ON_FAIL(layoutImpl->init(desc));
    returnComPtr(outLayout, layoutImpl);
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

    RootShaderObjectLayoutImpl::create(
        this,
        shaderProgram->linkedProgram,
        shaderProgram->linkedProgram->getLayout(),
        shaderProgram->m_rootObjectLayout.writeRef()
    );

    if (!shaderProgram->isSpecializable())
    {
        SLANG_RETURN_ON_FAIL(shaderProgram->compileShaders(this));
    }

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

Result DeviceImpl::createShaderObject(ShaderObjectLayout* layout, IShaderObject** outObject)
{
    AUTORELEASEPOOL

    RefPtr<ShaderObjectImpl> shaderObject;
    SLANG_RETURN_ON_FAIL(
        ShaderObjectImpl::create(this, static_cast<ShaderObjectLayoutImpl*>(layout), shaderObject.writeRef())
    );
    returnComPtr(outObject, shaderObject);
    return SLANG_OK;
}

Result DeviceImpl::createMutableShaderObject(ShaderObjectLayout* layout, IShaderObject** outObject)
{
    AUTORELEASEPOOL

    return SLANG_E_NOT_IMPLEMENTED;
}

Result DeviceImpl::createMutableRootShaderObject(IShaderProgram* program, IShaderObject** outObject)
{
    AUTORELEASEPOOL

    return SLANG_E_NOT_IMPLEMENTED;
}

Result DeviceImpl::createShaderTable(const IShaderTable::Desc& desc, IShaderTable** outShaderTable)
{
    AUTORELEASEPOOL

    return SLANG_E_NOT_IMPLEMENTED;
}

Result DeviceImpl::createRenderPipeline(const RenderPipelineDesc& desc, IPipeline** outPipeline)
{
    AUTORELEASEPOOL

    RefPtr<PipelineImpl> pipelineImpl = new PipelineImpl(this);
    pipelineImpl->init(desc);
    returnComPtr(outPipeline, pipelineImpl);
    return SLANG_OK;
}

Result DeviceImpl::createComputePipeline(const ComputePipelineDesc& desc, IPipeline** outPipeline)
{
    AUTORELEASEPOOL

    RefPtr<PipelineImpl> pipelineImpl = new PipelineImpl(this);
    pipelineImpl->init(desc);
    m_deviceObjectsWithPotentialBackReferences.push_back(pipelineImpl);
    returnComPtr(outPipeline, pipelineImpl);
    return SLANG_OK;
}

Result DeviceImpl::createRayTracingPipeline(const RayTracingPipelineDesc& desc, IPipeline** outPipeline)
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

Result DeviceImpl::createFence(const FenceDesc& desc, IFence** outFence)
{
    AUTORELEASEPOOL

    RefPtr<FenceImpl> fenceImpl = new FenceImpl();
    SLANG_RETURN_ON_FAIL(fenceImpl->init(this, desc));
    returnComPtr(outFence, fenceImpl);
    return SLANG_OK;
}

Result DeviceImpl::waitForFences(
    GfxCount fenceCount,
    IFence** fences,
    uint64_t* fenceValues,
    bool waitForAll,
    uint64_t timeout
)
{
    return SLANG_E_NOT_IMPLEMENTED;
}

} // namespace rhi::metal
