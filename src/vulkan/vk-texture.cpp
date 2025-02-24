#include "vk-texture.h"
#include "vk-device.h"
#include "vk-buffer.h"
#include "vk-util.h"
#include "vk-helper-functions.h"

#if !SLANG_WINDOWS_FAMILY
#include <unistd.h>
#endif

namespace rhi::vk {

TextureImpl::TextureImpl(DeviceImpl* device, const TextureDesc& desc)
    : Texture(desc)
    , m_device(device)
{
}

TextureImpl::~TextureImpl()
{
    auto& api = m_device->m_api;
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

    auto& api = m_device->m_api;
    PFN_vkGetMemoryWin32HandleKHR vkCreateSharedHandle;
    vkCreateSharedHandle = api.vkGetMemoryWin32HandleKHR;
    if (!vkCreateSharedHandle)
    {
        return SLANG_FAIL;
    }
    SLANG_RETURN_ON_FAIL(vkCreateSharedHandle(m_device->m_device, &info, (HANDLE*)&m_sharedHandle.value) != VK_SUCCESS);
    m_sharedHandle.type = NativeHandleType::Win32;
#else
    VkMemoryGetFdInfoKHR info = {};
    info.sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR;
    info.pNext = nullptr;
    info.memory = m_imageMemory;
    info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;

    auto& api = m_device->m_api;
    PFN_vkGetMemoryFdKHR vkCreateSharedHandle;
    vkCreateSharedHandle = api.vkGetMemoryFdKHR;
    if (!vkCreateSharedHandle)
    {
        return SLANG_FAIL;
    }
    SLANG_RETURN_ON_FAIL(vkCreateSharedHandle(m_device->m_device, &info, (int*)&m_sharedHandle.value) != VK_SUCCESS);
    m_sharedHandle.type = NativeHandleType::FileDescriptor;
#endif
    *outHandle = m_sharedHandle;
    return SLANG_OK;
}

TextureSubresourceView TextureImpl::getView(Format format, TextureAspect aspect, const SubresourceRange& range)
{
    ViewKey key = {format, aspect, range};

    std::lock_guard<std::mutex> lock(m_mutex);

    TextureSubresourceView& view = m_views[key];
    if (view.imageView)
        return view;

    bool isArray = m_desc.arrayLength > 1;
    VkImageViewCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    createInfo.flags = 0;
    createInfo.format = getFormatInfo(format).isTypeless ? VulkanUtil::getVkFormat(format) : m_vkformat;
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
        createInfo.viewType = isArray ? VK_IMAGE_VIEW_TYPE_1D_ARRAY : VK_IMAGE_VIEW_TYPE_1D;
        break;
    case TextureType::Texture2D:
        createInfo.viewType = isArray ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
        break;
    case TextureType::Texture3D:
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_3D;
        break;
    case TextureType::TextureCube:
        createInfo.viewType = isArray ? VK_IMAGE_VIEW_TYPE_CUBE_ARRAY : VK_IMAGE_VIEW_TYPE_CUBE;
        break;
    }

    createInfo.subresourceRange.aspectMask = getAspectMaskFromFormat(m_vkformat, aspect);

    createInfo.subresourceRange.baseArrayLayer = range.baseArrayLayer;
    createInfo.subresourceRange.baseMipLevel = range.mipLevel;
    createInfo.subresourceRange.layerCount = range.layerCount;
    createInfo.subresourceRange.levelCount = range.mipLevelCount;

    VkResult result =
        m_device->m_api.vkCreateImageView(m_device->m_api.m_device, &createInfo, nullptr, &view.imageView);
    SLANG_RHI_ASSERT(result == VK_SUCCESS);
    return view;
}

Result TextureViewImpl::getNativeHandle(NativeHandle* outHandle)
{
    return SLANG_E_NOT_AVAILABLE;
}

TextureSubresourceView TextureViewImpl::getView()
{
    return m_texture->getView(m_desc.format, m_desc.aspect, m_desc.subresourceRange);
}

Result DeviceImpl::createTexture(const TextureDesc& descIn, const SubresourceData* initData, ITexture** outTexture)
{
    TextureDesc desc = fixupTextureDesc(descIn);

    const VkFormat format = VulkanUtil::getVkFormat(desc.format);
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
    {
        imageInfo.imageType = VK_IMAGE_TYPE_1D;
        imageInfo.extent = VkExtent3D{uint32_t(descIn.size.width), 1, 1};
        break;
    }
    case TextureType::Texture2D:
    {
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = VkExtent3D{uint32_t(descIn.size.width), uint32_t(descIn.size.height), 1};
        break;
    }
    case TextureType::TextureCube:
    {
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = VkExtent3D{uint32_t(descIn.size.width), uint32_t(descIn.size.height), 1};
        imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        break;
    }
    case TextureType::Texture3D:
    {
        // Can't have an array and 3d texture
        SLANG_RHI_ASSERT(desc.arrayLength <= 1);
        imageInfo.imageType = VK_IMAGE_TYPE_3D;
        imageInfo.extent =
            VkExtent3D{uint32_t(descIn.size.width), uint32_t(descIn.size.height), uint32_t(descIn.size.depth)};
        break;
    }
    default:
    {
        SLANG_RHI_ASSERT_FAILURE("Unhandled type");
        return SLANG_FAIL;
    }
    }

    uint32_t arrayLayerCount = desc.arrayLength * (desc.type == TextureType::TextureCube ? 6 : 1);

    imageInfo.mipLevels = desc.mipLevelCount;
    imageInfo.arrayLayers = arrayLayerCount;

    imageInfo.format = format;

    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = _calcImageUsageFlags(desc.usage, desc.memoryType, initData);
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    imageInfo.samples = (VkSampleCountFlagBits)desc.sampleCount;

    VkExternalMemoryImageCreateInfo externalMemoryImageCreateInfo = {VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO
    };
    VkExternalMemoryHandleTypeFlags extMemoryHandleType =
#if SLANG_WINDOWS_FAMILY
        VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
#else
        VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
#endif
    if (is_set(descIn.usage, TextureUsage::Shared))
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
    if (is_set(descIn.usage, TextureUsage::Shared))
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

    VKBufferHandleRAII uploadBuffer;
    if (initData)
    {
        std::vector<Extents> mipSizes;

        VkCommandBuffer commandBuffer = m_deviceQueue.getCommandBuffer();

        // Calculate how large the buffer has to be
        Size bufferSize = 0;
        // Calculate how large an array entry is
        for (uint32_t j = 0; j < desc.mipLevelCount; ++j)
        {
            const Extents mipSize = calcMipSize(desc.size, j);

            auto rowSizeInBytes = calcRowSize(desc.format, mipSize.width);
            auto numRows = calcNumRows(desc.format, mipSize.height);

            mipSizes.push_back(mipSize);

            bufferSize += (rowSizeInBytes * numRows) * mipSize.depth;
        }

        // Calculate the total size taking into account the array
        bufferSize *= arrayLayerCount;

        SLANG_RETURN_ON_FAIL(uploadBuffer.init(
            m_api,
            bufferSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        ));

        SLANG_RHI_ASSERT(mipSizes.size() == desc.mipLevelCount);

        // Copy into upload buffer
        {
            uint32_t subresourceCounter = 0;

            uint8_t* dstData;
            m_api.vkMapMemory(m_device, uploadBuffer.m_memory, 0, bufferSize, 0, (void**)&dstData);

            uint64_t dstSubresourceOffset = 0;
            for (uint32_t i = 0; i < arrayLayerCount; ++i)
            {
                for (uint32_t j = 0; j < mipSizes.size(); ++j)
                {
                    const auto& mipSize = mipSizes[j];

                    uint32_t subresourceIndex = subresourceCounter++;
                    auto initSubresource = initData[subresourceIndex];

                    const ptrdiff_t srcRowStride = (ptrdiff_t)initSubresource.strideY;
                    const ptrdiff_t srcLayerStride = (ptrdiff_t)initSubresource.strideZ;

                    auto dstRowSizeInBytes = calcRowSize(desc.format, mipSize.width);
                    auto numRows = calcNumRows(desc.format, mipSize.height);
                    auto dstLayerSizeInBytes = dstRowSizeInBytes * numRows;

                    const uint8_t* srcLayer = (const uint8_t*)initSubresource.data;
                    uint8_t* dstLayer = dstData + dstSubresourceOffset;

                    for (uint32_t k = 0; k < mipSize.depth; k++)
                    {
                        const uint8_t* srcRow = srcLayer;
                        uint8_t* dstRow = dstLayer;

                        for (uint32_t l = 0; l < numRows; l++)
                        {
                            ::memcpy(dstRow, srcRow, dstRowSizeInBytes);

                            dstRow += dstRowSizeInBytes;
                            srcRow += srcRowStride;
                        }

                        dstLayer += dstLayerSizeInBytes;
                        srcLayer += srcLayerStride;
                    }

                    dstSubresourceOffset += dstLayerSizeInBytes * mipSize.depth;
                }
            }

            m_api.vkUnmapMemory(m_device, uploadBuffer.m_memory);
        }

        _transitionImageLayout(
            texture->m_image,
            format,
            texture->m_desc,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
        );

        if (desc.sampleCount > 1)
        {
            // Handle senario where texture is sampled. We cannot use
            // a simple buffer copy for sampled textures. ClearColorImage
            // is not data accurate but it is fine for testing & works.
            const FormatInfo& formatInfo = getFormatInfo(desc.format);
            VkClearColorValue clearColor;
            switch (formatInfo.channelType)
            {
            case SLANG_SCALAR_TYPE_INT32:
                for (int i = 0; i < 4; i++)
                    clearColor.int32[i] = *reinterpret_cast<int32_t*>(const_cast<void*>(initData->data));
                break;
            case SLANG_SCALAR_TYPE_UINT32:
                for (int i = 0; i < 4; i++)
                    clearColor.uint32[i] = *reinterpret_cast<uint32_t*>(const_cast<void*>(initData->data));
                break;
            case SLANG_SCALAR_TYPE_INT64:
            {
                for (int i = 0; i < 4; i++)
                    clearColor.int32[i] = int32_t(*reinterpret_cast<int64_t*>(const_cast<void*>(initData->data)));
                break;
            }
            case SLANG_SCALAR_TYPE_UINT64:
            {
                for (int i = 0; i < 4; i++)
                    clearColor.uint32[i] = uint32_t(*reinterpret_cast<uint64_t*>(const_cast<void*>(initData->data)));
                break;
            }
            case SLANG_SCALAR_TYPE_FLOAT16:
            {
                for (int i = 0; i < 4; i++)
                    clearColor.float32[i] =
                        math::halfToFloat(*reinterpret_cast<uint16_t*>(const_cast<void*>(initData->data)));
                break;
            }
            case SLANG_SCALAR_TYPE_FLOAT32:
            {
                for (int i = 0; i < 4; i++)
                    clearColor.float32[i] = (*reinterpret_cast<float*>(const_cast<void*>(initData->data)));
                break;
            }
            case SLANG_SCALAR_TYPE_FLOAT64:
            {
                for (int i = 0; i < 4; i++)
                    clearColor.float32[i] = float(*reinterpret_cast<double*>(const_cast<void*>(initData->data)));
                break;
            }
            case SLANG_SCALAR_TYPE_INT8:
            {
                for (int i = 0; i < 4; i++)
                    clearColor.int32[i] = int32_t(*reinterpret_cast<int8_t*>(const_cast<void*>(initData->data)));
                break;
            }
            case SLANG_SCALAR_TYPE_UINT8:
            {
                for (int i = 0; i < 4; i++)
                    clearColor.uint32[i] = uint32_t(*reinterpret_cast<uint8_t*>(const_cast<void*>(initData->data)));
                break;
            }
            case SLANG_SCALAR_TYPE_INT16:
            {
                for (int i = 0; i < 4; i++)
                    clearColor.int32[i] = int32_t(*reinterpret_cast<int16_t*>(const_cast<void*>(initData->data)));
                break;
            }
            case SLANG_SCALAR_TYPE_UINT16:
            {
                for (int i = 0; i < 4; i++)
                    clearColor.uint32[i] = uint32_t(*reinterpret_cast<uint16_t*>(const_cast<void*>(initData->data)));
                break;
            }
            };

            VkImageSubresourceRange range{};
            range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            range.baseMipLevel = 0;
            range.levelCount = VK_REMAINING_MIP_LEVELS;
            range.baseArrayLayer = 0;
            range.layerCount = VK_REMAINING_ARRAY_LAYERS;

            m_api.vkCmdClearColorImage(
                commandBuffer,
                texture->m_image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                &clearColor,
                1,
                &range
            );
        }
        else
        {
            uint64_t srcOffset = 0;
            for (int i = 0; i < arrayLayerCount; ++i)
            {
                for (size_t j = 0; j < mipSizes.size(); ++j)
                {
                    const auto& mipSize = mipSizes[j];

                    auto rowSizeInBytes = calcRowSize(desc.format, mipSize.width);
                    auto numRows = calcNumRows(desc.format, mipSize.height);

                    // https://www.khronos.org/registry/vulkan/specs/1.1-extensions/man/html/VkBufferImageCopy.html
                    // bufferRowLength and bufferImageHeight specify the data in buffer memory as a
                    // subregion of a larger two- or three-dimensional image, and control the
                    // addressing calculations of data in buffer memory. If either of these values
                    // is zero, that aspect of the buffer memory is considered to be tightly packed
                    // according to the imageExtent.

                    VkBufferImageCopy region = {};

                    region.bufferOffset = srcOffset;
                    region.bufferRowLength = 0; // rowSizeInBytes;
                    region.bufferImageHeight = 0;

                    region.imageSubresource.aspectMask = getAspectMaskFromFormat(format);
                    region.imageSubresource.mipLevel = uint32_t(j);
                    region.imageSubresource.baseArrayLayer = i;
                    region.imageSubresource.layerCount = 1;
                    region.imageOffset = {0, 0, 0};
                    region.imageExtent = {uint32_t(mipSize.width), uint32_t(mipSize.height), uint32_t(mipSize.depth)};

                    // Do the copy (do all depths in a single go)
                    m_api.vkCmdCopyBufferToImage(
                        commandBuffer,
                        uploadBuffer.m_buffer,
                        texture->m_image,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        1,
                        &region
                    );

                    // Next
                    srcOffset += rowSizeInBytes * numRows * mipSize.depth;
                }
            }
        }
        auto defaultLayout = VulkanUtil::getImageLayoutFromState(desc.defaultState);
        _transitionImageLayout(
            texture->m_image,
            format,
            texture->m_desc,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            defaultLayout
        );
    }
    else
    {
        auto defaultLayout = VulkanUtil::getImageLayoutFromState(desc.defaultState);
        if (defaultLayout != VK_IMAGE_LAYOUT_UNDEFINED)
        {
            _transitionImageLayout(texture->m_image, format, texture->m_desc, VK_IMAGE_LAYOUT_UNDEFINED, defaultLayout);
        }
    }
    m_deviceQueue.flushAndWait();
    returnComPtr(outTexture, texture);
    return SLANG_OK;
}

Result DeviceImpl::createTextureView(ITexture* texture, const TextureViewDesc& desc, ITextureView** outView)
{
    RefPtr<TextureViewImpl> view = new TextureViewImpl(desc);
    view->m_texture = checked_cast<TextureImpl*>(texture);
    if (view->m_desc.format == Format::Unknown)
        view->m_desc.format = view->m_texture->m_desc.format;
    view->m_desc.subresourceRange = view->m_texture->resolveSubresourceRange(desc.subresourceRange);
    returnComPtr(outView, view);
    return SLANG_OK;
}

} // namespace rhi::vk
