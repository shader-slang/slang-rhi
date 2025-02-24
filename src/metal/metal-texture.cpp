#include "metal-texture.h"
#include "metal-device.h"
#include "metal-util.h"

namespace rhi::metal {

TextureImpl::TextureImpl(const TextureDesc& desc)
    : Texture(desc)
{
}

TextureImpl::~TextureImpl() {}

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

Result TextureViewImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::MTLTexture;
    outHandle->value = (uint64_t)m_textureView.get();
    return SLANG_OK;
}

Result DeviceImpl::createTexture(const TextureDesc& descIn, const SubresourceData* initData, ITexture** outTexture)
{
    AUTORELEASEPOOL

    TextureDesc desc = fixupTextureDesc(descIn);

    // Metal doesn't support mip-mapping for 1D textures
    // However, we still need to use the provided mip level count when initializing the texture
    uint32_t initMipLevels = desc.mipLevelCount;
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
        default:
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

        uint32_t sliceCount = desc.arrayLength * (desc.type == TextureType::TextureCube ? 6 : 1);

        for (uint32_t slice = 0; slice < sliceCount; ++slice)
        {
            MTL::Region region;
            region.origin = MTL::Origin(0, 0, 0);
            region.size = MTL::Size(desc.size.width, desc.size.height, desc.size.depth);
            for (uint32_t level = 0; level < initMipLevels; ++level)
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
    RefPtr<TextureViewImpl> viewImpl = new TextureViewImpl(desc);
    viewImpl->m_texture = textureImpl;
    if (viewImpl->m_desc.format == Format::Unknown)
        viewImpl->m_desc.format = viewImpl->m_texture->m_desc.format;
    viewImpl->m_desc.subresourceRange = viewImpl->m_texture->resolveSubresourceRange(desc.subresourceRange);

    const TextureDesc& textureDesc = textureImpl->m_desc;
    uint32_t layerCount = textureDesc.arrayLength * (textureDesc.type == TextureType::TextureCube ? 6 : 1);
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
    NS::Range levelRange(sr.mipLevel, sr.mipLevelCount);
    NS::Range sliceRange(sr.baseArrayLayer, sr.layerCount);

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
