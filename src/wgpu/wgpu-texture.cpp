#include "wgpu-texture.h"
#include "wgpu-device.h"
#include "wgpu-utils.h"

#include "core/deferred.h"

namespace rhi::wgpu {

TextureImpl::TextureImpl(Device* device, const TextureDesc& desc)
    : Texture(device, desc)
{
}

TextureImpl::~TextureImpl()
{
    m_defaultView.setNull();
    if (m_texture)
    {
        getDevice<DeviceImpl>()->m_ctx.api.wgpuTextureRelease(m_texture);
    }
}

Result TextureImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::WGPUTexture;
    outHandle->value = (uint64_t)m_texture;
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

Result DeviceImpl::createTexture(const TextureDesc& desc_, const SubresourceData* initData, ITexture** outTexture)
{
    TextureDesc desc = fixupTextureDesc(desc_);

    // WebGPU only supports 1 MIP level for 1d textures
    // https://www.w3.org/TR/webgpu/#abstract-opdef-maximum-miplevel-count
    if ((desc.type == TextureType::Texture1D) && (desc.mipCount > 1))
    {
        return SLANG_FAIL;
    }

    // WebGPU does not support 1D texture arrays
    if ((desc.type == TextureType::Texture1D) && (desc.arrayLength > 1))
    {
        return SLANG_FAIL;
    }

    RefPtr<TextureImpl> texture = new TextureImpl(this, desc);
    WGPUTextureDescriptor textureDesc = {};

    textureDesc.size.width = desc.size.width;
    textureDesc.size.height = desc.size.height;
    textureDesc.size.depthOrArrayLayers = desc.getLayerCount();
    textureDesc.mipLevelCount = desc.mipCount;
    textureDesc.sampleCount = desc.sampleCount;
    textureDesc.format = translateTextureFormat(desc.format);
    textureDesc.label = translateString(desc.label);
    textureDesc.usage = translateTextureUsage(desc.usage);
    if (initData)
    {
        textureDesc.usage |= WGPUTextureUsage_CopyDst;
    }

    switch (desc.type)
    {
    case TextureType::Texture1D:
        // 1D texture with mip levels is not supported in WebGPU.
        if (desc.mipCount > 1)
        {
            return SLANG_E_NOT_AVAILABLE;
        }
        textureDesc.dimension = WGPUTextureDimension_1D;
        break;
    case TextureType::Texture1DArray:
        // 1D texture array is not supported in WebGPU.
        return SLANG_E_NOT_AVAILABLE;
        break;
    case TextureType::Texture2D:
    case TextureType::Texture2DArray:
    case TextureType::Texture2DMS:
    case TextureType::Texture2DMSArray:
    case TextureType::TextureCube:
    case TextureType::TextureCubeArray:
        textureDesc.dimension = WGPUTextureDimension_2D;
        break;
    case TextureType::Texture3D:
        textureDesc.dimension = WGPUTextureDimension_3D;
        textureDesc.size.depthOrArrayLayers = desc.size.depth;
        break;
    }

    texture->m_texture = m_ctx.api.wgpuDeviceCreateTexture(m_ctx.device, &textureDesc);
    if (!texture->m_texture)
    {
        return SLANG_FAIL;
    }

    // Upload init data if we have some
    if (initData)
    {
        ComPtr<ICommandQueue> queue;
        SLANG_RETURN_ON_FAIL(getQueue(QueueType::Graphics, queue.writeRef()));

        ComPtr<ICommandEncoder> commandEncoder;
        SLANG_RETURN_ON_FAIL(queue->createCommandEncoder(commandEncoder.writeRef()));

        SubresourceRange range;
        range.layer = 0;
        range.layerCount = desc.getLayerCount();
        range.mip = 0;
        range.mipCount = desc.mipCount;

        commandEncoder->uploadTextureData(
            texture,
            range,
            {0, 0, 0},
            Extent3D::kWholeTexture,
            initData,
            range.layerCount * desc.mipCount
        );

        SLANG_RETURN_ON_FAIL(queue->submit(commandEncoder->finish()));
    }

    returnComPtr(outTexture, texture);
    return SLANG_OK;
}


TextureViewImpl::TextureViewImpl(Device* device, const TextureViewDesc& desc)
    : TextureView(device, desc)
{
}

TextureViewImpl::~TextureViewImpl()
{
    if (m_textureView)
    {
        getDevice<DeviceImpl>()->m_ctx.api.wgpuTextureViewRelease(m_textureView);
    }
}

Result TextureViewImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::WGPUTextureView;
    outHandle->value = (uint64_t)m_textureView;
    return SLANG_OK;
}

Result DeviceImpl::createTextureView(ITexture* texture, const TextureViewDesc& desc, ITextureView** outView)
{
    TextureImpl* textureImpl = checked_cast<TextureImpl*>(texture);
    RefPtr<TextureViewImpl> view = new TextureViewImpl(this, desc);
    view->m_texture = textureImpl;
    view->m_desc.subresourceRange = view->m_texture->resolveSubresourceRange(view->m_desc.subresourceRange);

    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.format =
        translateTextureFormat(desc.format == Format::Undefined ? textureImpl->m_desc.format : desc.format);
    viewDesc.dimension = translateTextureViewDimension(textureImpl->m_desc.type);
    viewDesc.baseMipLevel = view->m_desc.subresourceRange.mip;
    viewDesc.mipLevelCount = view->m_desc.subresourceRange.mipCount;
    viewDesc.baseArrayLayer = view->m_desc.subresourceRange.layer;
    viewDesc.arrayLayerCount = view->m_desc.subresourceRange.layerCount;
    viewDesc.aspect = translateTextureAspect(desc.aspect);
    viewDesc.label = translateString(desc.label);

    view->m_textureView = m_ctx.api.wgpuTextureCreateView(textureImpl->m_texture, &viewDesc);
    if (!view->m_textureView)
    {
        return SLANG_FAIL;
    }

    returnComPtr(outView, view);
    return SLANG_OK;
}

} // namespace rhi::wgpu
