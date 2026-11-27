#include "vk-texture.h"
#include "vk-device.h"
#include "vk-buffer.h"
#include "vk-resource-heap.h"
#include "vk-utils.h"

#if !SLANG_WINDOWS_FAMILY
#include <unistd.h>
#endif

namespace rhi::vk {

Result getVkImageCreateInfo(const TextureDesc& desc, bool addCopyDestination, VkImageCreateInfo* outImageInfo)
{
    const VkFormat format = getVkFormat(desc.format);
    if (format == VK_FORMAT_UNDEFINED)
        return SLANG_FAIL;

    VkImageCreateInfo imageInfo = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    switch (desc.type)
    {
    case TextureType::Texture1D:
    case TextureType::Texture1DArray:
        imageInfo.imageType = VK_IMAGE_TYPE_1D;
        imageInfo.extent = VkExtent3D{desc.size.width, 1, 1};
        break;
    case TextureType::Texture2D:
    case TextureType::Texture2DArray:
    case TextureType::Texture2DMS:
    case TextureType::Texture2DMSArray:
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = VkExtent3D{desc.size.width, desc.size.height, 1};
        break;
    case TextureType::Texture3D:
        imageInfo.imageType = VK_IMAGE_TYPE_3D;
        imageInfo.extent = VkExtent3D{desc.size.width, desc.size.height, desc.size.depth};
        if (is_set(desc.usage, TextureUsage::RenderTarget))
            imageInfo.flags |= VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT;
        break;
    case TextureType::TextureCube:
    case TextureType::TextureCubeArray:
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = VkExtent3D{desc.size.width, desc.size.height, 1};
        imageInfo.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        break;
    }

    if (is_set(desc.usage, TextureUsage::Typeless))
        imageInfo.flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;

    imageInfo.mipLevels = desc.mipCount;
    imageInfo.arrayLayers = desc.getLayerCount();
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = _calcImageUsageFlags(desc.usage, desc.memoryType, nullptr);
    if (addCopyDestination)
        imageInfo.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = (VkSampleCountFlagBits)desc.sampleCount;

    *outImageInfo = imageInfo;
    return SLANG_OK;
}

TextureImpl::TextureImpl(Device* device, const TextureDesc& desc)
    : Texture(device, desc)
{
}

TextureImpl::~TextureImpl()
{
    destroyDefaultView();
    DeviceImpl* device = getDevice<DeviceImpl>();
    const auto& api = device->m_api;
    for (auto& view : m_views)
    {
        api.vkDestroyImageView(api.m_device, view.second.imageView, nullptr);
    }
    if (m_shouldDestroyImage)
    {
        device->m_memoryAllocator.destroyImage(m_image, m_allocation);
    }
    if (m_sharedHandle)
    {
#if SLANG_WINDOWS_FAMILY
        ::CloseHandle((HANDLE)m_sharedHandle.value);
#else
        ::close((int)m_sharedHandle.value);
#endif
    }
}

void TextureImpl::deleteThis()
{
    if (m_isSwapchainTexture)
    {
        delete this;
        return;
    }
    m_sampler.setNull();
    getDevice<DeviceImpl>()->deferDelete(this);
}

Result TextureImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::VkImage;
    outHandle->value = (uint64_t)m_image;
    return SLANG_OK;
}

Result TextureImpl::getSharedHandle(NativeHandle* outHandle)
{
    // Only textures created with TextureUsage::Shared own exportable memory. All other textures are
    // sub-allocated from memory blocks shared with unrelated resources (or placed in a resource heap).
    if (!m_sharedHandle)
    {
        if (!is_set(m_desc.usage, TextureUsage::Shared) || !m_allocation)
            return SLANG_E_INVALID_ARG;
        SLANG_RETURN_ON_FAIL(getDevice<DeviceImpl>()->m_memoryAllocator.exportHandle(m_allocation, &m_sharedHandle));
    }
    *outHandle = m_sharedHandle;
    return SLANG_OK;
}

TextureImpl::View TextureImpl::getView(
    Format format,
    TextureAspect aspect,
    const SubresourceRange& range,
    bool isRenderTarget
)
{
    DeviceImpl* device = getDevice<DeviceImpl>();

    ViewKey key = {format, aspect, range, isRenderTarget};
    View& view = m_views[key];
    if (view.imageView)
        return view;

    VkImageViewCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    createInfo.flags = 0;
    createInfo.format = getVkFormat(format);
    createInfo.image = m_image;
    createInfo.components = VkComponentMapping{
        VK_COMPONENT_SWIZZLE_R,
        VK_COMPONENT_SWIZZLE_G,
        VK_COMPONENT_SWIZZLE_B,
        VK_COMPONENT_SWIZZLE_A
    };
    switch (m_desc.type)
    {
    case TextureType::Texture1D:
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_1D;
        break;
    case TextureType::Texture1DArray:
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_1D_ARRAY;
        break;
    case TextureType::Texture2D:
    case TextureType::Texture2DMS:
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        break;
    case TextureType::Texture2DArray:
    case TextureType::Texture2DMSArray:
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        break;
    case TextureType::Texture3D:
        // 3D textures are bound as 2D arrays when used as render targets.
        createInfo.viewType = isRenderTarget ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_3D;
        break;
    case TextureType::TextureCube:
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
        break;
    case TextureType::TextureCubeArray:
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
        break;
    }

    createInfo.subresourceRange.aspectMask = getAspectMaskFromFormat(m_vkformat, aspect);

    createInfo.subresourceRange.baseArrayLayer = range.layer;
    createInfo.subresourceRange.baseMipLevel = range.mip;
    createInfo.subresourceRange.layerCount = range.layerCount;
    createInfo.subresourceRange.levelCount = range.mipCount;

    if (isRenderTarget && m_desc.type == TextureType::Texture3D)
    {
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = m_desc.size.depth;
    }

    VkResult result = device->m_api.vkCreateImageView(device->m_api.m_device, &createInfo, nullptr, &view.imageView);
    SLANG_RHI_ASSERT(result == VK_SUCCESS);
    return view;
}

TextureViewImpl::TextureViewImpl(TextureImpl* texture, const TextureViewDesc& desc)
    : TextureView(texture, desc)
    , m_texture(texture)
{
}

TextureViewImpl::~TextureViewImpl()
{
    DeviceImpl* device = getDevice<DeviceImpl>();

    for (auto& handle : m_descriptorHandle)
    {
        if (handle)
        {
            device->m_bindlessDescriptorSet->freeHandle(handle);
        }
    }
}

Result TextureViewImpl::getNativeHandle(NativeHandle* outHandle)
{
    TextureImpl::View view = getView();
    outHandle->type = NativeHandleType::VkImageView;
    outHandle->value = (uint64_t)view.imageView;
    return SLANG_OK;
}

Result TextureViewImpl::getDescriptorHandle(DescriptorHandleAccess access, DescriptorHandle* outHandle)
{
    DescriptorHandle& handle = m_descriptorHandle[access == DescriptorHandleAccess::Read ? 0 : 1];

    if (handle)
    {
        *outHandle = handle;
        return SLANG_OK;
    }

    DeviceImpl* device = getDevice<DeviceImpl>();

    if (!device->m_bindlessDescriptorSet)
    {
        return SLANG_E_NOT_AVAILABLE;
    }

    if (!handle)
    {
        SLANG_RETURN_ON_FAIL(device->m_bindlessDescriptorSet->allocTextureHandle(this, access, &handle));
    }

    *outHandle = handle;
    return SLANG_OK;
}

Result TextureViewImpl::getCombinedTextureSamplerDescriptorHandle(DescriptorHandle* outHandle)
{
    DescriptorHandle& handle = m_descriptorHandle[2];

    if (handle)
    {
        *outHandle = handle;
        return SLANG_OK;
    }

    DeviceImpl* device = getDevice<DeviceImpl>();

    if (!device->m_bindlessDescriptorSet)
    {
        return SLANG_E_NOT_AVAILABLE;
    }

    if (!handle)
    {
        Sampler* sampler = m_sampler ? m_sampler : m_texture->m_sampler;
        if (!sampler)
        {
            return SLANG_FAIL;
        }
        SLANG_RETURN_ON_FAIL(
            device->m_bindlessDescriptorSet->allocCombinedTextureSamplerHandle(this, sampler, &handle)
        );
    }

    *outHandle = handle;
    return SLANG_OK;
}

TextureImpl::View TextureViewImpl::getView()
{
    return m_texture->getView(m_desc.format, m_desc.aspect, m_desc.subresourceRange, false);
}

TextureImpl::View TextureViewImpl::getRenderTargetView()
{
    return m_texture->getView(m_desc.format, m_desc.aspect, m_desc.subresourceRange, true);
}

Result DeviceImpl::createTexture(const TextureDesc& desc_, const SubresourceData* initData, ITexture** outTexture)
{
    TextureDesc desc = fixupTextureDesc(desc_);

    if (desc.heap)
        SLANG_RETURN_ON_FAIL(checkTexturePlacementSupported(desc));
    VkImageCreateInfo imageInfo = {};
    SLANG_RETURN_ON_FAIL(getVkImageCreateInfo(desc, initData != nullptr || desc.heap != nullptr, &imageInfo));
    const VkFormat format = imageInfo.format;
    const uint32_t layerCount = desc.getLayerCount();

    RefPtr<TextureImpl> texture(new TextureImpl(this, desc));
    texture->m_vkformat = format;

    if (desc.heap)
    {
        // Placed texture: bind the image to memory owned by the resource heap.
        SLANG_VK_RETURN_ON_FAIL_REPORT(m_api.vkCreateImage(m_device, &imageInfo, nullptr, &texture->m_image), this);

        VkMemoryRequirements memRequirements;
        m_api.vkGetImageMemoryRequirements(m_device, texture->m_image, &memRequirements);

        ResourceMemoryRequirements requirements = {};
        SLANG_RETURN_ON_FAIL(getTextureMemoryRequirements(desc, &requirements));
        SLANG_RETURN_ON_FAIL(validateResourcePlacement(this, desc.heap, desc.heapOffset, requirements));

        ResourceHeapImpl* heap = checked_cast<ResourceHeapImpl*>(desc.heap);
        const VmaAllocationInfo heapInfo = m_memoryAllocator.getAllocationInfo(heap->m_allocation);
        if ((memRequirements.memoryTypeBits & (1u << heapInfo.memoryType)) == 0)
            return SLANG_E_INVALID_ARG;

        SLANG_VK_RETURN_ON_FAIL_REPORT(
            m_api.vkBindImageMemory(
                m_device,
                texture->m_image,
                heapInfo.deviceMemory,
                heapInfo.offset + desc.heapOffset
            ),
            this
        );
        texture->setPlacement(heap, desc.heapOffset, requirements.size);
    }
    else
    {
        // Shared textures get dedicated, exportable memory.
        AllocationDesc allocationDesc;
        allocationDesc.memoryType = MemoryType::DeviceLocal;
        allocationDesc.exportable = is_set(desc.usage, TextureUsage::Shared);
        SLANG_RETURN_ON_FAIL(
            m_memoryAllocator.createImage(imageInfo, allocationDesc, &texture->m_image, &texture->m_allocation)
        );
    }

    _labelObject((uint64_t)texture->m_image, VK_OBJECT_TYPE_IMAGE, desc.label);

    // Transition the image out of VK_IMAGE_LAYOUT_UNDEFINED and upload init data (if any) using a regular
    // command buffer, like D3D12 does for init data. The command buffer keeps the texture alive until it
    // has executed. Placed textures without init data are initialized when activated via aliasResources().
    if (!desc.heap || initData)
    {
        ComPtr<ICommandQueue> queue;
        SLANG_RETURN_ON_FAIL(getQueue(QueueType::Graphics, queue.writeRef()));

        ComPtr<ICommandEncoder> commandEncoder;
        SLANG_RETURN_ON_FAIL(queue->createCommandEncoder(commandEncoder.writeRef()));

        // The next barrier (to copy destination for init data, otherwise to the default state at the end
        // of the command buffer) transitions from the undefined layout.
        checked_cast<CommandEncoder*>(commandEncoder.get())->initializeTexture(texture);

        if (initData)
        {
            SubresourceRange range;
            range.layer = 0;
            range.layerCount = layerCount;
            range.mip = 0;
            range.mipCount = desc.mipCount;

            SLANG_RETURN_ON_FAIL(commandEncoder->uploadTextureData(
                texture,
                range,
                {0, 0, 0},
                Extent3D::kWholeTexture,
                initData,
                layerCount * desc.mipCount
            ));
        }

        SLANG_RETURN_ON_FAIL(queue->submit(commandEncoder->finish()));

        // Shared textures can be accessed by other APIs immediately after creation.
        if (is_set(desc.usage, TextureUsage::Shared))
        {
            SLANG_RETURN_ON_FAIL(queue->waitOnHost());
        }
    }

    returnComPtr(outTexture, texture);
    return SLANG_OK;
}

Result DeviceImpl::createTextureFromNativeHandle(NativeHandle handle, const TextureDesc& desc_, ITexture** outTexture)
{
    if (handle.type != NativeHandleType::VkImage || handle.value == 0)
    {
        *outTexture = nullptr;
        return SLANG_E_INVALID_HANDLE;
    }

    TextureDesc desc = fixupTextureDesc(desc_);

    const VkFormat format = getVkFormat(desc.format);
    if (format == VK_FORMAT_UNDEFINED)
    {
        return SLANG_E_INVALID_ARG;
    }

    VkImage nativeImage = (VkImage)handle.value;
    RefPtr<TextureImpl> texture(new TextureImpl(this, desc));
    texture->m_image = nativeImage;
    texture->m_vkformat = format;
    texture->m_shouldDestroyImage = false;

    _labelObject((uint64_t)texture->m_image, VK_OBJECT_TYPE_IMAGE, desc.label);

    returnComPtr(outTexture, texture);
    return SLANG_OK;
}

Result DeviceImpl::createTextureView(ITexture* texture, const TextureViewDesc& desc, ITextureView** outView)
{
    RefPtr<TextureViewImpl> view = new TextureViewImpl(checked_cast<TextureImpl*>(texture), desc);
    if (view->m_desc.format == Format::Undefined)
        view->m_desc.format = view->m_texture->m_desc.format;
    view->m_desc.subresourceRange = view->m_texture->resolveSubresourceRange(desc.subresourceRange);
    returnComPtr(outView, view);
    return SLANG_OK;
}

} // namespace rhi::vk
