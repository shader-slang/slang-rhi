#include "vk-texture.h"
#include "vk-device.h"
#include "vk-buffer.h"
#include "vk-utils.h"

#if SLANG_WINDOWS_FAMILY
#include <dxgi1_4.h>
#endif

#if !SLANG_WINDOWS_FAMILY
#include <unistd.h>
#endif

namespace rhi::vk {

TextureImpl::TextureImpl(Device* device, const TextureDesc& desc)
    : Texture(device, desc)
{
}

TextureImpl::~TextureImpl()
{
    m_defaultView.setNull();
    DeviceImpl* device = getDevice<DeviceImpl>();
    auto& api = device->m_api;
    for (auto& view : m_views)
    {
        api.vkDestroyImageView(api.m_device, view.second.imageView, nullptr);
    }
    if (!m_isWeakImageReference)
    {
        api.vkFreeMemory(api.m_device, m_imageMemory, nullptr);
        api.vkDestroyImage(api.m_device, m_image, nullptr);
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

Result TextureImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::VkImage;
    outHandle->value = (uint64_t)m_image;
    return SLANG_OK;
}

Result TextureImpl::getSharedHandle(NativeHandle* outHandle)
{
    DeviceImpl* device = getDevice<DeviceImpl>();

    // Check if a shared handle already exists for this resource.
    if (m_sharedHandle)
    {
        *outHandle = m_sharedHandle;
        return SLANG_OK;
    }

    // If a shared handle doesn't exist, create one and store it.
#if SLANG_WINDOWS_FAMILY
    VkMemoryGetWin32HandleInfoKHR info = {};
    info.sType = VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR;
    info.pNext = nullptr;
    info.memory = m_imageMemory;
    info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;

    auto& api = device->m_api;
    PFN_vkGetMemoryWin32HandleKHR vkCreateSharedHandle;
    vkCreateSharedHandle = api.vkGetMemoryWin32HandleKHR;
    if (!vkCreateSharedHandle)
    {
        return SLANG_FAIL;
    }
    SLANG_RETURN_ON_FAIL(vkCreateSharedHandle(device->m_device, &info, (HANDLE*)&m_sharedHandle.value) != VK_SUCCESS);
    m_sharedHandle.type = NativeHandleType::Win32;
#else
    VkMemoryGetFdInfoKHR info = {};
    info.sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR;
    info.pNext = nullptr;
    info.memory = m_imageMemory;
    info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;

    auto& api = device->m_api;
    PFN_vkGetMemoryFdKHR vkCreateSharedHandle;
    vkCreateSharedHandle = api.vkGetMemoryFdKHR;
    if (!vkCreateSharedHandle)
    {
        return SLANG_FAIL;
    }
    SLANG_RETURN_ON_FAIL(vkCreateSharedHandle(device->m_device, &info, (int*)&m_sharedHandle.value) != VK_SUCCESS);
    m_sharedHandle.type = NativeHandleType::FileDescriptor;
#endif
    *outHandle = m_sharedHandle;
    return SLANG_OK;
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

TextureSubresourceView TextureImpl::getView(
    Format format,
    TextureAspect aspect,
    const SubresourceRange& range,
    bool isRenderTarget
)
{
    DeviceImpl* device = getDevice<DeviceImpl>();

    ViewKey key = {format, aspect, range, isRenderTarget};

    std::lock_guard<std::mutex> lock(m_mutex);

    TextureSubresourceView& view = m_views[key];
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

Result TextureViewImpl::getNativeHandle(NativeHandle* outHandle)
{
    return SLANG_E_NOT_AVAILABLE;
}

Result TextureViewImpl::getDescriptorHandle(DescriptorHandleAccess access, DescriptorHandle* outHandle)
{
    DeviceImpl* device = getDevice<DeviceImpl>();

    if (!device->m_bindlessDescriptorSet)
    {
        return SLANG_E_NOT_AVAILABLE;
    }
    DescriptorHandle& handle = m_descriptorHandle[access == DescriptorHandleAccess::Read ? 0 : 1];
    if (!handle)
    {
        SLANG_RETURN_ON_FAIL(device->m_bindlessDescriptorSet->allocTextureHandle(this, access, &handle));
    }
    *outHandle = handle;
    return SLANG_OK;
}

TextureSubresourceView TextureViewImpl::getView()
{
    return m_texture->getView(m_desc.format, m_desc.aspect, m_desc.subresourceRange, false);
}

TextureSubresourceView TextureViewImpl::getRenderTargetView()
{
    return m_texture->getView(m_desc.format, m_desc.aspect, m_desc.subresourceRange, true);
}

Result DeviceImpl::createTexture(const TextureDesc& desc_, const SubresourceData* initData, ITexture** outTexture)
{
    TextureDesc desc = fixupTextureDesc(desc_);

    const VkFormat format = getVkFormat(desc.format);
    if (format == VK_FORMAT_UNDEFINED)
    {
        SLANG_RHI_ASSERT_FAILURE("Unhandled image format");
        return SLANG_FAIL;
    }

    RefPtr<TextureImpl> texture(new TextureImpl(this, desc));
    texture->m_vkformat = format;

    // Create the image
    VkImageCreateInfo imageInfo = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    switch (desc.type)
    {
    case TextureType::Texture1D:
    case TextureType::Texture1DArray:
    {
        imageInfo.imageType = VK_IMAGE_TYPE_1D;
        imageInfo.extent = VkExtent3D{desc.size.width, 1, 1};
        break;
    }
    case TextureType::Texture2D:
    case TextureType::Texture2DArray:
    case TextureType::Texture2DMS:
    case TextureType::Texture2DMSArray:
    {
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = VkExtent3D{desc.size.width, desc.size.height, 1};
        break;
    }
    case TextureType::Texture3D:
    {
        imageInfo.imageType = VK_IMAGE_TYPE_3D;
        imageInfo.extent = VkExtent3D{desc.size.width, desc.size.height, desc.size.depth};
        // When using 3D textures as render targets, we need to be able to create 2d array views.
        if (is_set(desc.usage, TextureUsage::RenderTarget))
        {
            imageInfo.flags = VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT;
        }
        break;
    }
    case TextureType::TextureCube:
    case TextureType::TextureCubeArray:
    {
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = VkExtent3D{desc.size.width, desc.size.height, 1};
        imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        break;
    }
    }

    if (is_set(desc.usage, TextureUsage::Typeless))
    {
        imageInfo.flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
    }

    uint32_t layerCount = desc.getLayerCount();

    imageInfo.mipLevels = desc.mipCount;
    imageInfo.arrayLayers = layerCount;

    imageInfo.format = format;

    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = _calcImageUsageFlags(desc.usage, desc.memoryType, initData);
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    imageInfo.samples = (VkSampleCountFlagBits)desc.sampleCount;

    VkExternalMemoryImageCreateInfo externalMemoryImageCreateInfo = {
        VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO
    };
    VkExternalMemoryHandleTypeFlags extMemoryHandleType =
#if SLANG_WINDOWS_FAMILY
        VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
#else
        VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
#endif
    if (is_set(desc.usage, TextureUsage::Shared))
    {
        externalMemoryImageCreateInfo.pNext = nullptr;
        externalMemoryImageCreateInfo.handleTypes = extMemoryHandleType;
        imageInfo.pNext = &externalMemoryImageCreateInfo;
    }
    SLANG_VK_RETURN_ON_FAIL(m_api.vkCreateImage(m_device, &imageInfo, nullptr, &texture->m_image));

    VkMemoryRequirements memRequirements;
    m_api.vkGetImageMemoryRequirements(m_device, texture->m_image, &memRequirements);

    // Allocate the memory
    VkMemoryPropertyFlags reqMemoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    int memoryTypeIndex = m_api.findMemoryTypeIndex(memRequirements.memoryTypeBits, reqMemoryProperties);
    SLANG_RHI_ASSERT(memoryTypeIndex >= 0);

    VkMemoryAllocateInfo allocInfo = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = memoryTypeIndex;
#if SLANG_WINDOWS_FAMILY
    VkExportMemoryWin32HandleInfoKHR exportMemoryWin32HandleInfo = {
        VK_STRUCTURE_TYPE_EXPORT_MEMORY_WIN32_HANDLE_INFO_KHR
    };
#endif
    VkExportMemoryAllocateInfoKHR exportMemoryAllocateInfo = {VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO_KHR};
    if (is_set(desc.usage, TextureUsage::Shared))
    {
#if SLANG_WINDOWS_FAMILY
        exportMemoryWin32HandleInfo.pNext = nullptr;
        exportMemoryWin32HandleInfo.pAttributes = nullptr;
        exportMemoryWin32HandleInfo.dwAccess = DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE;
        exportMemoryWin32HandleInfo.name = NULL;

        exportMemoryAllocateInfo.pNext = extMemoryHandleType & VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT_KHR
                                             ? &exportMemoryWin32HandleInfo
                                             : nullptr;
#endif
        exportMemoryAllocateInfo.handleTypes = extMemoryHandleType;
        allocInfo.pNext = &exportMemoryAllocateInfo;
    }
    SLANG_VK_RETURN_ON_FAIL(m_api.vkAllocateMemory(m_device, &allocInfo, nullptr, &texture->m_imageMemory));

    // Bind the memory to the image
    m_api.vkBindImageMemory(m_device, texture->m_image, texture->m_imageMemory, 0);

    _labelObject((uint64_t)texture->m_image, VK_OBJECT_TYPE_IMAGE, desc.label);

    // Transition to default layout
    auto defaultLayout = getImageLayoutFromState(desc.defaultState);
    if (defaultLayout != VK_IMAGE_LAYOUT_UNDEFINED)
    {
        _transitionImageLayout(texture->m_image, format, texture->m_desc, VK_IMAGE_LAYOUT_UNDEFINED, defaultLayout);
    }
    m_deviceQueue.flushAndWait();

    // Upload init data if we have some
    if (initData)
    {
        ComPtr<ICommandQueue> queue;
        SLANG_RETURN_ON_FAIL(getQueue(QueueType::Graphics, queue.writeRef()));

        ComPtr<ICommandEncoder> commandEncoder;
        SLANG_RETURN_ON_FAIL(queue->createCommandEncoder(commandEncoder.writeRef()));

        SubresourceRange range;
        range.layer = 0;
        range.layerCount = layerCount;
        range.mip = 0;
        range.mipCount = desc.mipCount;

        commandEncoder->uploadTextureData(
            texture,
            range,
            {0, 0, 0},
            Extent3D::kWholeTexture,
            initData,
            layerCount * desc.mipCount
        );

        SLANG_RETURN_ON_FAIL(queue->submit(commandEncoder->finish()));
    }

    returnComPtr(outTexture, texture);
    return SLANG_OK;
}

Result DeviceImpl::createTextureView(ITexture* texture, const TextureViewDesc& desc, ITextureView** outView)
{
    RefPtr<TextureViewImpl> view = new TextureViewImpl(this, desc);
    view->m_texture = checked_cast<TextureImpl*>(texture);
    if (view->m_desc.format == Format::Undefined)
        view->m_desc.format = view->m_texture->m_desc.format;
    view->m_desc.subresourceRange = view->m_texture->resolveSubresourceRange(desc.subresourceRange);
    returnComPtr(outView, view);
    return SLANG_OK;
}

} // namespace rhi::vk
