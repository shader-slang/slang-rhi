#include "wgpu-texture.h"
#include "wgpu-device.h"
#include "wgpu-util.h"

#include "core/deferred.h"

namespace rhi::wgpu {

TextureImpl::TextureImpl(DeviceImpl* device, const TextureDesc& desc)
    : Texture(desc)
    , m_device(device)
{
}

TextureImpl::~TextureImpl()
{
    if (m_texture)
    {
        m_device->m_ctx.api.wgpuTextureRelease(m_texture);
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

Result DeviceImpl::createTexture(const TextureDesc& desc_, const SubresourceData* initData, ITexture** outTexture)
{
    TextureDesc desc = fixupTextureDesc(desc_);

    RefPtr<TextureImpl> texture = new TextureImpl(this, desc);
    WGPUTextureDescriptor textureDesc = {};
    textureDesc.size.width = desc.size.width;
    textureDesc.size.height = desc.size.height;
    textureDesc.size.depthOrArrayLayers = desc.size.depth;
    textureDesc.usage = translateTextureUsage(desc.usage);
    if (initData)
    {
        textureDesc.usage |= WGPUTextureUsage_CopyDst;
    }
    textureDesc.dimension = translateTextureDimension(desc.type);
    textureDesc.format = translateTextureFormat(desc.format);
    textureDesc.mipLevelCount = desc.mipLevelCount;
    textureDesc.sampleCount = desc.sampleCount;
    textureDesc.label = desc.label;
    texture->m_texture = m_ctx.api.wgpuDeviceCreateTexture(m_ctx.device, &textureDesc);
    if (!texture->m_texture)
    {
        return SLANG_FAIL;
    }

    if (initData)
    {
        const FormatInfo& formatInfo = getFormatInfo(desc.format);

        WGPUQueue queue = m_ctx.api.wgpuDeviceGetQueue(m_ctx.device);
        SLANG_RHI_DEFERRED({ m_ctx.api.wgpuQueueRelease(queue); });
        int mipLevelCount = desc.mipLevelCount;
        int arrayLayerCount = desc.arrayLength * (desc.type == TextureType::TextureCube ? 6 : 1);

        for (int arrayLayer = 0; arrayLayer < arrayLayerCount; ++arrayLayer)
        {
            for (int mipLevel = 0; mipLevel < mipLevelCount; ++mipLevel)
            {
                Extents mipSize = calcMipSize(desc.size, mipLevel);
                int subresourceIndex = arrayLayer * mipLevelCount + mipLevel;
                const SubresourceData& data = initData[subresourceIndex];

                WGPUImageCopyTexture imageCopyTexture = {};
                imageCopyTexture.texture = texture->m_texture;
                imageCopyTexture.mipLevel = mipLevel;
                imageCopyTexture.origin = {0, 0, 0};
                imageCopyTexture.aspect = WGPUTextureAspect_All;

                WGPUExtent3D writeSize = {};
                writeSize.width =
                    ((mipSize.width + formatInfo.blockWidth - 1) / formatInfo.blockWidth) * formatInfo.blockWidth;
                writeSize.height =
                    ((mipSize.height + formatInfo.blockHeight - 1) / formatInfo.blockHeight) * formatInfo.blockHeight;
                writeSize.depthOrArrayLayers = mipSize.depth;

                WGPUTextureDataLayout dataLayout = {};
                dataLayout.offset = 0;
                dataLayout.bytesPerRow = data.strideY;
                dataLayout.rowsPerImage = writeSize.height / formatInfo.blockHeight;

                size_t dataSize = dataLayout.bytesPerRow * dataLayout.rowsPerImage * mipSize.depth;

                m_ctx.api.wgpuQueueWriteTexture(queue, &imageCopyTexture, data.data, dataSize, &dataLayout, &writeSize);
            }
        }

        // Wait for queue to finish.
        // TODO: we should switch to the new async API
        {
            WGPUQueueWorkDoneStatus status = WGPUQueueWorkDoneStatus_Unknown;
            WGPUQueueWorkDoneCallbackInfo2 callbackInfo = {};
            callbackInfo.nextInChain = nullptr;
            callbackInfo.mode = WGPUCallbackMode_WaitAnyOnly;
            callbackInfo.callback = [](WGPUQueueWorkDoneStatus status, void* userdata1, void* userdata2)
            { *(WGPUQueueWorkDoneStatus*)userdata1 = status; };
            callbackInfo.userdata1 = &status;
            callbackInfo.userdata2 = nullptr;
            WGPUFuture future = m_ctx.api.wgpuQueueOnSubmittedWorkDone2(queue, callbackInfo);
            constexpr size_t futureCount = size_t{1};
            WGPUFutureWaitInfo futures[futureCount] = {future};
            uint64_t timeoutNS = UINT64_MAX;
            WGPUWaitStatus waitStatus = m_ctx.api.wgpuInstanceWaitAny(m_ctx.instance, futureCount, futures, timeoutNS);
            if (waitStatus != WGPUWaitStatus_Success)
            {
                return SLANG_FAIL;
            }

            if (status != WGPUQueueWorkDoneStatus_Success)
            {
                return SLANG_FAIL;
            }
        }
    }

    returnComPtr(outTexture, texture);
    return SLANG_OK;
}


TextureViewImpl::TextureViewImpl(DeviceImpl* device, const TextureViewDesc& desc)
    : TextureView(desc)
    , m_device(device)
{
}

TextureViewImpl::~TextureViewImpl()
{
    if (m_textureView)
    {
        m_device->m_ctx.api.wgpuTextureViewRelease(m_textureView);
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
    viewDesc.format = translateTextureFormat(desc.format == Format::Unknown ? textureImpl->m_desc.format : desc.format);
    viewDesc.dimension = translateTextureViewDimension(textureImpl->m_desc.type, textureImpl->m_desc.arrayLength > 1);
    viewDesc.baseMipLevel = view->m_desc.subresourceRange.mipLevel;
    viewDesc.mipLevelCount = view->m_desc.subresourceRange.mipLevelCount;
    viewDesc.baseArrayLayer = view->m_desc.subresourceRange.baseArrayLayer;
    viewDesc.arrayLayerCount = view->m_desc.subresourceRange.layerCount;
    viewDesc.aspect = translateTextureAspect(desc.aspect);
    viewDesc.label = desc.label;

    view->m_textureView = m_ctx.api.wgpuTextureCreateView(textureImpl->m_texture, &viewDesc);
    if (!view->m_textureView)
    {
        return SLANG_FAIL;
    }

    returnComPtr(outView, view);
    return SLANG_OK;
}

} // namespace rhi::wgpu
