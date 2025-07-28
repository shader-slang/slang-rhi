#include "metal-texture.h"
#include "metal-device.h"
#include "metal-utils.h"

namespace rhi::metal {

TextureImpl::TextureImpl(Device* device, const TextureDesc& desc)
    : Texture(device, desc)
{
}

TextureImpl::~TextureImpl()
{
    m_defaultView.setNull();
}

Result TextureImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::MTLTexture;
    outHandle->value = (uint64_t)m_texture.get();
    return SLANG_OK;
}

Result TextureImpl::getSharedHandle(NativeHandle* outHandle)
{
    *outHandle = {};
    return SLANG_E_NOT_AVAILABLE;
}

Result TextureImpl::getDefaultView(ITextureView** outTextureView)
{
    if (!m_defaultView)
    {
        SLANG_RETURN_ON_FAIL(m_device->createTextureView(this, {}, (ITextureView**)m_defaultView.writeRef()));
        m_defaultView->setInternalReferenceCount(1);
    }
    returnComPtr(outTextureView, m_defaultView);
    return SLANG_OK;
}

TextureViewImpl::TextureViewImpl(Device* device, const TextureViewDesc& desc)
    : TextureView(device, desc)
{
}

Result TextureViewImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::MTLTexture;
    outHandle->value = (uint64_t)m_textureView.get();
    return SLANG_OK;
}

Result DeviceImpl::createTexture(const TextureDesc& desc_, const SubresourceData* initData, ITexture** outTexture)
{
    AUTORELEASEPOOL

    TextureDesc desc = fixupTextureDesc(desc_);

    // Metal doesn't support mip-mapping for 1D textures
    if ((desc.type == TextureType::Texture1D || desc.type == TextureType::Texture1DArray) && desc.mipCount > 1)
    {
        return SLANG_E_NOT_AVAILABLE;
    }
    // Metal doesn't support multi-sampled textures with 1 sample
    if ((desc.type == TextureType::Texture2DMS || desc.type == TextureType::Texture2DMSArray) && desc.sampleCount == 1)
    {
        return SLANG_E_NOT_AVAILABLE;
    }

    const MTL::PixelFormat pixelFormat = translatePixelFormat(desc.format);
    if (pixelFormat == MTL::PixelFormat::PixelFormatInvalid)
    {
        SLANG_RHI_ASSERT_FAILURE("Unsupported texture format");
        return SLANG_FAIL;
    }

    RefPtr<TextureImpl> textureImpl(new TextureImpl(this, desc));

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

    textureDesc->setTextureType(translateTextureType(desc.type));
    textureDesc->setWidth(desc.size.width);
    textureDesc->setHeight(desc.size.height);
    textureDesc->setDepth(desc.size.depth);
    textureDesc->setMipmapLevelCount(desc.mipCount);
    textureDesc->setArrayLength(desc.arrayLength);
    textureDesc->setPixelFormat(pixelFormat);
    textureDesc->setSampleCount(desc.sampleCount);

    MTL::TextureUsage textureUsage = MTL::TextureUsageUnknown;
    if (is_set(desc.usage, TextureUsage::RenderTarget) || is_set(desc.usage, TextureUsage::DepthStencil))
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

        // TODO: We should check if atomics are supported.
        // Request atomic access if the format allows it.
        switch (desc.format)
        {
        case Format::R32Uint:
        case Format::R32Sint:
            textureUsage |= MTL::TextureUsageShaderAtomic;
            break;
        default:
            break;
        }
    }

    textureDesc->setUsage(textureUsage);
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
        textureImpl->m_texture->setLabel(createString(desc.label).get());
    }

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

        uint32_t sliceCount = desc.getLayerCount();

        for (uint32_t slice = 0; slice < sliceCount; ++slice)
        {
            MTL::Region region;
            region.origin = MTL::Origin(0, 0, 0);
            region.size = MTL::Size(desc.size.width, desc.size.height, desc.size.depth);
            for (uint32_t level = 0; level < desc.mipCount; ++level)
            {
                if (level >= desc.mipCount)
                    continue;
                const SubresourceData& subresourceData = initData[slice * desc.mipCount + level];
                stagingTexture->replaceRegion(
                    region,
                    level,
                    slice,
                    subresourceData.data,
                    subresourceData.rowPitch,
                    subresourceData.slicePitch
                );
                encoder->synchronizeTexture(stagingTexture.get(), slice, level);
                region.size.width = region.size.width > 0 ? max(1ul, region.size.width >> 1) : 0;
                region.size.height = region.size.height > 0 ? max(1ul, region.size.height >> 1) : 0;
                region.size.depth = region.size.depth > 0 ? max(1ul, region.size.depth >> 1) : 0;
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

Result DeviceImpl::createTextureView(ITexture* texture, const TextureViewDesc& desc, ITextureView** outView)
{
    AUTORELEASEPOOL

    auto textureImpl = checked_cast<TextureImpl*>(texture);
    RefPtr<TextureViewImpl> viewImpl = new TextureViewImpl(this, desc);
    viewImpl->m_texture = textureImpl;
    if (viewImpl->m_desc.format == Format::Undefined)
        viewImpl->m_desc.format = viewImpl->m_texture->m_desc.format;
    viewImpl->m_desc.subresourceRange = viewImpl->m_texture->resolveSubresourceRange(desc.subresourceRange);

    const TextureDesc& textureDesc = textureImpl->m_desc;
    uint32_t layerCount = textureDesc.arrayLength * (textureDesc.type == TextureType::TextureCube ? 6 : 1);
    SubresourceRange sr = viewImpl->m_desc.subresourceRange;
    if (sr.layer == 0 && sr.layerCount == layerCount && sr.mip == 0 && sr.mipCount == textureDesc.mipCount)
    {
        viewImpl->m_textureView = textureImpl->m_texture;
        returnComPtr(outView, viewImpl);
        return SLANG_OK;
    }

    MTL::PixelFormat pixelFormat =
        desc.format == Format::Undefined ? textureImpl->m_pixelFormat : translatePixelFormat(desc.format);
    NS::Range sliceRange(sr.layer, sr.layerCount);
    NS::Range levelRange(sr.mip, sr.mipCount);

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

} // namespace rhi::metal
