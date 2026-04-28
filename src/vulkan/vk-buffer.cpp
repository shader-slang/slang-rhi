#include "vk-buffer.h"
#include "vk-device.h"
#include "vk-utils.h"

#if SLANG_WINDOWS_FAMILY
#include <dxgi1_4.h>
#endif

#if !SLANG_WINDOWS_FAMILY
#include <unistd.h>
#endif


namespace rhi::vk {

// Helper function to create a VkBuffer with optional external memory support
Result createVkBuffer(
    const VulkanApi& api,
    Size bufferSize,
    VkBufferUsageFlags usage,
    VkExternalMemoryHandleTypeFlagsKHR externalMemoryHandleTypeFlags,
    VkBuffer* outBuffer
)
{
    VkBufferCreateInfo bufferCreateInfo = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferCreateInfo.size = bufferSize;
    bufferCreateInfo.usage = usage;
    bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkExternalMemoryBufferCreateInfo externalMemoryBufferCreateInfo = {
        VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO
    };
    if (externalMemoryHandleTypeFlags)
    {
        externalMemoryBufferCreateInfo.handleTypes = externalMemoryHandleTypeFlags;
        bufferCreateInfo.pNext = &externalMemoryBufferCreateInfo;
    }

    SLANG_VK_RETURN_ON_FAIL(api.vkCreateBuffer(api.m_device, &bufferCreateInfo, nullptr, outBuffer));
    return SLANG_OK;
}

// Helper function to allocate VkDeviceMemory with optional external memory support
Result allocateVkMemory(
    const VulkanApi& api,
    const VkMemoryRequirements& memoryReqs,
    VkMemoryPropertyFlags reqMemoryProperties,
    bool needsDeviceAddress,
    VkExternalMemoryHandleTypeFlagsKHR externalMemoryHandleTypeFlags,
    VkDeviceMemory* outMemory
)
{
    int memoryTypeIndex = api.findMemoryTypeIndex(memoryReqs.memoryTypeBits, reqMemoryProperties);
    SLANG_RHI_ASSERT(memoryTypeIndex >= 0);

    VkMemoryAllocateInfo allocateInfo = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocateInfo.allocationSize = memoryReqs.size;
    allocateInfo.memoryTypeIndex = memoryTypeIndex;

#if SLANG_WINDOWS_FAMILY
    VkExportMemoryWin32HandleInfoKHR exportMemoryWin32HandleInfo = {
        VK_STRUCTURE_TYPE_EXPORT_MEMORY_WIN32_HANDLE_INFO_KHR
    };
#endif
    VkExportMemoryAllocateInfoKHR exportMemoryAllocateInfo = {VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO_KHR};
    if (externalMemoryHandleTypeFlags)
    {
#if SLANG_WINDOWS_FAMILY
        exportMemoryWin32HandleInfo.pNext = nullptr;
        exportMemoryWin32HandleInfo.pAttributes = nullptr;
        exportMemoryWin32HandleInfo.dwAccess = DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE;
        exportMemoryWin32HandleInfo.name = NULL;

        exportMemoryAllocateInfo.pNext =
            externalMemoryHandleTypeFlags & VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT_KHR
                ? &exportMemoryWin32HandleInfo
                : nullptr;
#endif
        exportMemoryAllocateInfo.handleTypes = externalMemoryHandleTypeFlags;
        allocateInfo.pNext = &exportMemoryAllocateInfo;
    }

    VkMemoryAllocateFlagsInfo flagInfo = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
    if (needsDeviceAddress)
    {
        flagInfo.deviceMask = 1;
        flagInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

        flagInfo.pNext = allocateInfo.pNext;
        allocateInfo.pNext = &flagInfo;
    }

    SLANG_VK_RETURN_ON_FAIL(api.vkAllocateMemory(api.m_device, &allocateInfo, nullptr, outMemory));
    return SLANG_OK;
}

// Helper function to allocate VkDeviceMemory for a buffer with optional external memory support
Result allocateVkMemoryForBuffer(
    const VulkanApi& api,
    VkBuffer buffer,
    VkBufferUsageFlags bufferUsage,
    VkMemoryPropertyFlags reqMemoryProperties,
    VkExternalMemoryHandleTypeFlagsKHR externalMemoryHandleTypeFlags,
    VkDeviceMemory* outMemory
)
{
    VkMemoryRequirements memoryReqs = {};
    api.vkGetBufferMemoryRequirements(api.m_device, buffer, &memoryReqs);

    bool needsDeviceAddress = (bufferUsage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) != 0;

    return allocateVkMemory(
        api,
        memoryReqs,
        reqMemoryProperties,
        needsDeviceAddress,
        externalMemoryHandleTypeFlags,
        outMemory
    );
}

Result VKBufferHandleRAII::init(
    const VulkanApi& api,
    Size bufferSize,
    VkBufferUsageFlags usage,
    MemoryType memoryType,
    VkExternalMemoryHandleTypeFlagsKHR externalMemoryHandleTypeFlags
)
{
    SLANG_RHI_ASSERT(!isInitialized());

    m_api = &api;
    m_memory = VK_NULL_HANDLE;
    m_buffer = VK_NULL_HANDLE;

    VkMemoryPropertyFlags reqMemoryProperties = 0;
    switch (memoryType)
    {
    case MemoryType::Upload:
        reqMemoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    case MemoryType::ReadBack:
        reqMemoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                              VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
    case MemoryType::DeviceLocal:
    default:
        reqMemoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    }

    // Create buffer
    SLANG_RETURN_ON_FAIL(createVkBuffer(api, bufferSize, usage, externalMemoryHandleTypeFlags, &m_buffer));

    // Allocate memory
    SLANG_RETURN_ON_FAIL(
        allocateVkMemoryForBuffer(api, m_buffer, usage, reqMemoryProperties, externalMemoryHandleTypeFlags, &m_memory)
    );

    // Bind buffer to memory
    SLANG_VK_RETURN_ON_FAIL(api.vkBindBufferMemory(api.m_device, m_buffer, m_memory, 0));

    return SLANG_OK;
}

Result VKBufferHandleRAII::init(
    const VulkanApi& api,
    VmaAllocator vmaAllocator,
    Size bufferSize,
    VkBufferUsageFlags usage,
    MemoryType memoryType
)
{
    SLANG_RHI_ASSERT(!isInitialized());

    m_api = &api;
    m_buffer = VK_NULL_HANDLE;
    m_memory = VK_NULL_HANDLE;
    m_vmaAllocator = vmaAllocator;
    m_vmaAllocation = VK_NULL_HANDLE;

    VkBufferCreateInfo bufferCreateInfo = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferCreateInfo.size = bufferSize;
    bufferCreateInfo.usage = usage;
    bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocCreateInfo = {};
    switch (memoryType)
    {
    case MemoryType::DeviceLocal:
        allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        allocCreateInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        break;
    case MemoryType::Upload:
        allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
        allocCreateInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
        allocCreateInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        break;
    case MemoryType::ReadBack:
        allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
        allocCreateInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
        allocCreateInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        allocCreateInfo.preferredFlags = VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
        break;
    }

    // When VMA sub-allocates within a larger memory block, the resulting buffer device
    // address is offset within that block. Compute a minimum alignment from the device
    // limits based on usage flags so that the device address meets all spec requirements.
    VkDeviceSize minAlignment = 0;
    if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
    {
        const auto& limits = api.m_deviceProperties.limits;
        if (usage & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
            minAlignment = std::max(minAlignment, limits.minStorageBufferOffsetAlignment);
        if (usage & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
            minAlignment = std::max(minAlignment, limits.minUniformBufferOffsetAlignment);
        if (usage & (VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT))
            minAlignment = std::max(minAlignment, limits.minTexelBufferOffsetAlignment);
        // 256 bytes covers transform matrix (64B) and instance data (64B) alignment
        // requirements per the Vulkan spec for acceleration structure build inputs.
        if (usage & VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR)
            minAlignment = std::max(minAlignment, (VkDeviceSize)256);
        // VkAccelerationStructureCreateInfoKHR::offset must be a multiple of 256.
        if (usage & VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR)
            minAlignment = std::max(minAlignment, (VkDeviceSize)256);
        if (usage & VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR)
            minAlignment =
                std::max(minAlignment, (VkDeviceSize)api.m_rayTracingPipelineProperties.shaderGroupBaseAlignment);
        // Floor of 64 bytes covers spec-mandated device address alignment requirements
        // not exposed as queryable limits (e.g. vkCmdConvertCooperativeVectorMatrixNV).
        minAlignment = std::max(minAlignment, (VkDeviceSize)64);
    }

    SLANG_VK_RETURN_ON_FAIL(vmaCreateBufferWithAlignment(
        m_vmaAllocator,
        &bufferCreateInfo,
        &allocCreateInfo,
        minAlignment,
        &m_buffer,
        &m_vmaAllocation,
        nullptr
    ));

    // Retrieve the VkDeviceMemory for compatibility with code that needs it.
    // NOTE: With VMA sub-allocation this may be shared with other allocations and
    // must never be used directly without the allocation offset. All code paths
    // must check m_vmaAllocation first and use VMA functions when it is non-null.
    VmaAllocationInfo allocInfo;
    vmaGetAllocationInfo(m_vmaAllocator, m_vmaAllocation, &allocInfo);
    m_memory = allocInfo.deviceMemory;

    return SLANG_OK;
}

BufferImpl::BufferImpl(Device* device, const BufferDesc& desc)
    : Buffer(device, desc)
{
}

BufferImpl::~BufferImpl()
{
    DeviceImpl* device = getDevice<DeviceImpl>();

    for (auto& handle : m_descriptorHandles)
    {
        if (handle.second)
        {
            device->m_bindlessDescriptorSet->freeHandle(handle.second);
        }
    }

    for (auto& view : m_views)
    {
        m_buffer.m_api->vkDestroyBufferView(m_buffer.m_api->m_device, view.second, nullptr);
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

void BufferImpl::deleteThis()
{
    getDevice<DeviceImpl>()->deferDelete(this);
}

Result BufferImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::VkBuffer;
    outHandle->value = (uint64_t)m_buffer.m_buffer;
    return SLANG_OK;
}

Result BufferImpl::getSharedHandle(NativeHandle* outHandle)
{
    if (m_sharedHandle)
    {
        *outHandle = m_sharedHandle;
        return SLANG_OK;
    }

    DeviceImpl* device = getDevice<DeviceImpl>();
    const auto& api = device->m_api;

    // If a shared handle doesn't exist, create one and store it.
    // Shared buffers use dedicated (non-VMA) allocations, so m_memory is the
    // exclusive backing memory and can be exported directly.
    SLANG_RHI_ASSERT(!m_buffer.m_vmaAllocation && "getSharedHandle requires dedicated (non-VMA) allocation");
    if (!m_sharedHandle)
    {
#if SLANG_WINDOWS_FAMILY
        VkMemoryGetWin32HandleInfoKHR info = {};
        info.sType = VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR;
        info.pNext = nullptr;
        info.memory = m_buffer.m_memory;
        info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;

        if (!api.vkGetMemoryWin32HandleKHR)
        {
            return SLANG_FAIL;
        }
        HANDLE handle = NULL;
        SLANG_VK_RETURN_ON_FAIL(api.vkGetMemoryWin32HandleKHR(api.m_device, &info, &handle));
        m_sharedHandle = NativeHandle{NativeHandleType::Win32, (uint64_t)handle};
#else
        VkMemoryGetFdInfoKHR info = {};
        info.sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR;
        info.pNext = nullptr;
        info.memory = m_buffer.m_memory;
        info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;

        if (!api.vkGetMemoryFdKHR)
        {
            return SLANG_FAIL;
        }
        int handle = 0;
        SLANG_VK_RETURN_ON_FAIL(api.vkGetMemoryFdKHR(api.m_device, &info, &handle));
        m_sharedHandle = NativeHandle{NativeHandleType::FileDescriptor, (uint64_t)handle};
#endif
    }

    *outHandle = m_sharedHandle;
    return SLANG_OK;
}

DeviceAddress BufferImpl::getDeviceAddress()
{
    if (m_deviceAddress != 0)
    {
        return m_deviceAddress;
    }

    DeviceImpl* device = getDevice<DeviceImpl>();
    const auto& api = device->m_api;

    if (!api.vkGetBufferDeviceAddress)
    {
        return 0;
    }

    if (!m_deviceAddress)
    {
        VkBufferDeviceAddressInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        info.buffer = m_buffer.m_buffer;
        m_deviceAddress = (DeviceAddress)api.vkGetBufferDeviceAddress(device->m_device, &info);
    }

    return m_deviceAddress;
}

Result BufferImpl::getDescriptorHandle(
    DescriptorHandleAccess access,
    Format format,
    BufferRange range,
    DescriptorHandle* outHandle
)
{
    DeviceImpl* device = getDevice<DeviceImpl>();

    if (!device->m_bindlessDescriptorSet)
    {
        return SLANG_E_NOT_AVAILABLE;
    }

    range = resolveBufferRange(range);

    DescriptorHandleKey key = {access, format, range};
    DescriptorHandle& handle = m_descriptorHandles[key];
    if (handle)
    {
        *outHandle = handle;
        return SLANG_OK;
    }

    if (!handle)
    {
        SLANG_RETURN_ON_FAIL(device->m_bindlessDescriptorSet->allocBufferHandle(this, access, format, range, &handle));
    }

    *outHandle = handle;
    return SLANG_OK;
}

VkBufferView BufferImpl::getView(Format format, const BufferRange& range)
{
    ViewKey key = {format, range};
    VkBufferView& view = m_views[key];
    if (view)
        return view;

    VkBufferViewCreateInfo info = {VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO};
    info.format = getVkFormat(format);
    info.buffer = m_buffer.m_buffer;
    info.offset = range.offset;
    info.range = range.size;

    // VkBufferUsageFlags2CreateInfoKHR bufferViewUsage{};
    // bufferViewUsage.sType = VK_STRUCTURE_TYPE_BUFFER_USAGE_FLAGS_2_CREATE_INFO_KHR;

    // if (desc.type == IResourceView::Type::UnorderedAccess)
    // {
    //     info.pNext = &bufferViewUsage;
    //     bufferViewUsage.usage = VK_BUFFER_USAGE_2_STORAGE_TEXEL_BUFFER_BIT_KHR;
    // }
    // else if (desc.type == IResourceView::Type::ShaderResource)
    // {
    //     info.pNext = &bufferViewUsage;
    //     bufferViewUsage.usage = VK_BUFFER_USAGE_2_UNIFORM_TEXEL_BUFFER_BIT_KHR;
    // }
    // else
    // {
    //     SLANG_RHI_ASSERT_FAILURE("Unhandled");
    // }

    VkResult result = m_buffer.m_api->vkCreateBufferView(m_buffer.m_api->m_device, &info, nullptr, &view);
    SLANG_RHI_ASSERT(result == VK_SUCCESS);
    return view;
}

Result DeviceImpl::createBuffer(const BufferDesc& desc_, const void* initData, IBuffer** outBuffer)
{
    BufferDesc desc = fixupBufferDesc(desc_);

    const Size bufferSize = desc.size;

    VkBufferUsageFlags usage = _calcBufferUsageFlags(desc.usage);
    if (m_api.m_extendedFeatures.vulkan12Features.bufferDeviceAddress)
    {
        usage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    }
    if (is_set(desc.usage, BufferUsage::ShaderResource) &&
        m_api.m_extendedFeatures.accelerationStructureFeatures.accelerationStructure)
    {
        usage |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
    }
    if (initData)
    {
        usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    }

    MemoryType effectiveMemoryType = desc.memoryType;
    if (is_set(desc.usage, BufferUsage::ConstantBuffer) && desc.memoryType == MemoryType::DeviceLocal)
        effectiveMemoryType = MemoryType::Upload;

    RefPtr<BufferImpl> buffer(new BufferImpl(this, desc));
    if (is_set(desc.usage, BufferUsage::Shared))
    {
        VkExternalMemoryHandleTypeFlagsKHR externalMemoryHandleTypeFlags
#if SLANG_WINDOWS_FAMILY
            = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
#else
            = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
#endif
        SLANG_RETURN_ON_FAIL(
            buffer->m_buffer.init(m_api, desc.size, usage, effectiveMemoryType, externalMemoryHandleTypeFlags)
        );
    }
    else
    {
        SLANG_RETURN_ON_FAIL(buffer->m_buffer.init(m_api, m_vmaAllocator, desc.size, usage, effectiveMemoryType));
    }

    _labelObject((uint64_t)buffer->m_buffer.m_buffer, VK_OBJECT_TYPE_BUFFER, desc.label);

    if (initData)
    {
        if (desc.memoryType == MemoryType::DeviceLocal)
        {
            SLANG_RETURN_ON_FAIL(
                buffer->m_uploadBuffer
                    .init(m_api, m_vmaAllocator, bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, MemoryType::Upload)
            );
            // Copy into staging buffer
            void* mappedData = nullptr;
            if (buffer->m_uploadBuffer.m_vmaAllocation)
            {
                SLANG_VK_CHECK(vmaMapMemory(m_vmaAllocator, buffer->m_uploadBuffer.m_vmaAllocation, &mappedData));
            }
            else
            {
                SLANG_VK_CHECK(
                    m_api.vkMapMemory(m_device, buffer->m_uploadBuffer.m_memory, 0, bufferSize, 0, &mappedData)
                );
            }
            ::memcpy(mappedData, initData, bufferSize);
            if (buffer->m_uploadBuffer.m_vmaAllocation)
            {
                vmaUnmapMemory(m_vmaAllocator, buffer->m_uploadBuffer.m_vmaAllocation);
            }
            else
            {
                m_api.vkUnmapMemory(m_device, buffer->m_uploadBuffer.m_memory);
            }

            // Copy from staging buffer to real buffer
            VkCommandBuffer commandBuffer = m_deviceQueue.getCommandBuffer();

            VkBufferCopy copyInfo = {};
            copyInfo.size = bufferSize;
            m_api.vkCmdCopyBuffer(
                commandBuffer,
                buffer->m_uploadBuffer.m_buffer,
                buffer->m_buffer.m_buffer,
                1,
                &copyInfo
            );
            m_deviceQueue.flush();
        }
        else
        {
            // Copy into mapped buffer directly
            void* mappedData = nullptr;
            if (buffer->m_buffer.m_vmaAllocation)
            {
                SLANG_VK_CHECK(vmaMapMemory(m_vmaAllocator, buffer->m_buffer.m_vmaAllocation, &mappedData));
            }
            else
            {
                SLANG_VK_CHECK(m_api.vkMapMemory(m_device, buffer->m_buffer.m_memory, 0, bufferSize, 0, &mappedData));
            }
            ::memcpy(mappedData, initData, bufferSize);
            if (buffer->m_buffer.m_vmaAllocation)
            {
                vmaUnmapMemory(m_vmaAllocator, buffer->m_buffer.m_vmaAllocation);
            }
            else
            {
                m_api.vkUnmapMemory(m_device, buffer->m_buffer.m_memory);
            }
        }
    }

    returnComPtr(outBuffer, buffer);
    return SLANG_OK;
}

Result DeviceImpl::createBufferFromNativeHandle(NativeHandle handle, const BufferDesc& desc, IBuffer** outBuffer)
{
    RefPtr<BufferImpl> buffer(new BufferImpl(this, desc));

    if (handle.type == NativeHandleType::VkBuffer)
    {
        buffer->m_buffer.m_buffer = (VkBuffer)handle.value;
    }
    else
    {
        return SLANG_FAIL;
    }

    returnComPtr(outBuffer, buffer);
    return SLANG_OK;
}

Result DeviceImpl::mapBuffer(IBuffer* buffer, CpuAccessMode mode, void** outData)
{
    BufferImpl* bufferImpl = checked_cast<BufferImpl*>(buffer);
    if (bufferImpl->m_buffer.m_vmaAllocation)
    {
        SLANG_VK_RETURN_ON_FAIL(vmaMapMemory(m_vmaAllocator, bufferImpl->m_buffer.m_vmaAllocation, outData));
    }
    else
    {
        SLANG_VK_RETURN_ON_FAIL(
            m_api.vkMapMemory(m_api.m_device, bufferImpl->m_buffer.m_memory, 0, VK_WHOLE_SIZE, 0, outData)
        );
    }
    return SLANG_OK;
}

Result DeviceImpl::unmapBuffer(IBuffer* buffer)
{
    BufferImpl* bufferImpl = checked_cast<BufferImpl*>(buffer);
    if (bufferImpl->m_buffer.m_vmaAllocation)
    {
        vmaUnmapMemory(m_vmaAllocator, bufferImpl->m_buffer.m_vmaAllocation);
    }
    else
    {
        m_api.vkUnmapMemory(m_api.m_device, bufferImpl->m_buffer.m_memory);
    }
    return SLANG_OK;
}

} // namespace rhi::vk
