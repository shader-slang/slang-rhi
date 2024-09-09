#include "metal-device.h"
#include "../resource-desc-utils.h"
#include "metal-buffer.h"
#include "metal-shader-program.h"
#include "metal-swap-chain.h"
#include "metal-texture.h"
#include "metal-util.h"
#include "metal-vertex-layout.h"
// #include "metal-command-queue.h"
#include "metal-fence.h"
#include "metal-query.h"
// #include "metal-resource-views.h"
#include "metal-sampler.h"
#include "metal-shader-object-layout.h"
#include "metal-shader-object.h"
// #include "metal-shader-table.h"
#include "metal-transient-heap.h"
// #include "metal-pipeline-dump-layer.h"
// #include "metal-helper-functions.h"

#include "core/common.h"

#include <cstdio>
#include <vector>

namespace rhi::metal {

DeviceImpl::~DeviceImpl() {}

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

    SLANG_RETURN_ON_FAIL(RendererBase::initialize(desc));
    Result initDeviceResult = SLANG_OK;

    m_device = NS::TransferPtr(MTL::CreateSystemDefaultDevice());
    m_commandQueue = NS::TransferPtr(m_device->newCommandQueue(64));
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

Result DeviceImpl::createCommandQueue(const ICommandQueue::Desc& desc, ICommandQueue** outQueue)
{
    AUTORELEASEPOOL

    if (m_queueAllocCount != 0)
        return SLANG_FAIL;

    RefPtr<CommandQueueImpl> result = new CommandQueueImpl;
    result->init(this, m_commandQueue);
    returnComPtr(outQueue, result);
    // m_queueAllocCount++;
    return SLANG_OK;
}

Result DeviceImpl::createSwapchain(const ISwapchain::Desc& desc, WindowHandle window, ISwapchain** outSwapchain)
{
    AUTORELEASEPOOL

    RefPtr<SwapchainImpl> swapchainImpl = new SwapchainImpl();
    SLANG_RETURN_ON_FAIL(swapchainImpl->init(this, desc, window));
    returnComPtr(outSwapchain, swapchainImpl);
    return SLANG_OK;
}

Result DeviceImpl::readTexture(
    ITexture* texture,
    ResourceState state,
    ISlangBlob** outBlob,
    Size* outRowPitch,
    Size* outPixelSize
)
{
    AUTORELEASEPOOL

    TextureImpl* textureImpl = static_cast<TextureImpl*>(texture);

    if (textureImpl->getDesc()->sampleCount > 1)
    {
        return SLANG_E_NOT_IMPLEMENTED;
    }

    NS::SharedPtr<MTL::Texture> srcTexture = textureImpl->m_texture;

    const TextureDesc& desc = *textureImpl->getDesc();
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

Result DeviceImpl::getAccelerationStructurePrebuildInfo(
    const IAccelerationStructure::BuildInputs& buildInputs,
    IAccelerationStructure::PrebuildInfo* outPrebuildInfo
)
{
    AUTORELEASEPOOL

    return SLANG_E_NOT_IMPLEMENTED;
}

Result DeviceImpl::createAccelerationStructure(
    const IAccelerationStructure::CreateDesc& desc,
    IAccelerationStructure** outAS
)
{
    AUTORELEASEPOOL

    return SLANG_E_NOT_IMPLEMENTED;
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

    for (Int i = 0; i < desc.numMipLevels; ++i)
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
    size *= desc.arraySize ? desc.arraySize : 1;

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
    GfxCount initMipLevels = desc.numMipLevels;
    desc.numMipLevels = desc.type == TextureType::Texture1D ? 1 : desc.numMipLevels;

    const MTL::PixelFormat pixelFormat = MetalUtil::translatePixelFormat(desc.format);
    if (pixelFormat == MTL::PixelFormat::PixelFormatInvalid)
    {
        SLANG_RHI_ASSERT_FAILURE("Unsupported texture format");
        return SLANG_FAIL;
    }

    RefPtr<TextureImpl> textureImpl(new TextureImpl(desc, this));

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

    bool isArray = desc.arraySize > 0;

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

    textureDesc->setMipmapLevelCount(desc.numMipLevels);
    textureDesc->setArrayLength(isArray ? desc.arraySize : 1);
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

        GfxCount sliceCount = isArray ? desc.arraySize : 1;
        if (desc.type == TextureType::TextureCube)
        {
            sliceCount *= 6;
        }

        for (Index slice = 0; slice < sliceCount; ++slice)
        {
            MTL::Region region;
            region.origin = MTL::Origin(0, 0, 0);
            region.size = MTL::Size(desc.size.width, desc.size.height, desc.size.depth);
            for (Index level = 0; level < initMipLevels; ++level)
            {
                if (level >= desc.numMipLevels)
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

    RefPtr<BufferImpl> buffer(new BufferImpl(desc, this));
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

    RefPtr<SamplerImpl> samplerImpl = new SamplerImpl();
    SLANG_RETURN_ON_FAIL(samplerImpl->init(this, desc));
    returnComPtr(outSampler, samplerImpl);
    return SLANG_OK;
}

Result DeviceImpl::createTextureView(ITexture* texture, IResourceView::Desc const& desc, IResourceView** outView)
{
    AUTORELEASEPOOL

    auto textureImpl = static_cast<TextureImpl*>(texture);
    RefPtr<TextureViewImpl> viewImpl = new TextureViewImpl(this);
    viewImpl->m_desc = desc;
    viewImpl->m_device = this;
    viewImpl->m_texture = textureImpl;
    if (textureImpl == nullptr)
    {
        returnComPtr(outView, viewImpl);
        return SLANG_OK;
    }

    const TextureDesc& textureDesc = *textureImpl->getDesc();
    SubresourceRange sr = desc.subresourceRange;
    sr.mipLevelCount = sr.mipLevelCount == 0 ? textureDesc.numMipLevels - sr.mipLevel : sr.mipLevelCount;
    sr.layerCount = sr.layerCount == 0 ? textureDesc.arraySize - sr.baseArrayLayer : sr.layerCount;
    if (sr.mipLevel == 0 && sr.mipLevelCount == textureDesc.numMipLevels && sr.baseArrayLayer == 0 &&
        sr.layerCount == textureDesc.arraySize)
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

Result DeviceImpl::createBufferView(
    IBuffer* buffer,
    IBuffer* counterBuffer,
    IResourceView::Desc const& desc,
    IResourceView** outView
)
{
    AUTORELEASEPOOL

    // Counter buffers are not supported on metal.
    if (counterBuffer)
    {
        return SLANG_FAIL;
    }

    if (desc.type != IResourceView::Type::UnorderedAccess && desc.type != IResourceView::Type::ShaderResource)
    {
        return SLANG_FAIL;
    }

    auto bufferImpl = static_cast<BufferImpl*>(buffer);

    RefPtr<BufferViewImpl> viewImpl = new BufferViewImpl(this);
    viewImpl->m_desc = desc;
    viewImpl->m_buffer = bufferImpl;
    viewImpl->m_offset = desc.bufferRange.offset;
    viewImpl->m_size = desc.bufferRange.size == 0 ? bufferImpl->getDesc()->size : desc.bufferRange.size;
    returnComPtr(outView, viewImpl);
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
    ShaderObjectLayoutBase** outLayout
)
{
    AUTORELEASEPOOL

    RefPtr<ShaderObjectLayoutImpl> layout;
    SLANG_RETURN_ON_FAIL(ShaderObjectLayoutImpl::createForElementType(this, session, typeLayout, layout.writeRef()));
    returnRefPtrMove(outLayout, layout);
    return SLANG_OK;
}

Result DeviceImpl::createShaderObject(ShaderObjectLayoutBase* layout, IShaderObject** outObject)
{
    AUTORELEASEPOOL

    RefPtr<ShaderObjectImpl> shaderObject;
    SLANG_RETURN_ON_FAIL(
        ShaderObjectImpl::create(this, static_cast<ShaderObjectLayoutImpl*>(layout), shaderObject.writeRef())
    );
    returnComPtr(outObject, shaderObject);
    return SLANG_OK;
}

Result DeviceImpl::createMutableShaderObject(ShaderObjectLayoutBase* layout, IShaderObject** outObject)
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
