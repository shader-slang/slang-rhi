#include "vk-buffer.h"
#include "vk-device.h"
#include "vk-util.h"
#include "vk-helper-functions.h"

#if SLANG_WINDOWS_FAMILY
#include <dxgi1_2.h>
#endif

#if !SLANG_WINDOWS_FAMILY
#include <unistd.h>
#endif


namespace rhi::vk {

Result VKBufferHandleRAII::init(
    const VulkanApi& api,
    Size bufferSize,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags reqMemoryProperties,
    VkExternalMemoryHandleTypeFlagsKHR externalMemoryHandleTypeFlags
)
{
    SLANG_RHI_ASSERT(!isInitialized());

    m_api = &api;
    m_memory = VK_NULL_HANDLE;
    m_buffer = VK_NULL_HANDLE;

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

    SLANG_VK_RETURN_ON_FAIL(api.vkCreateBuffer(api.m_device, &bufferCreateInfo, nullptr, &m_buffer));

    VkMemoryRequirements memoryReqs = {};
    api.vkGetBufferMemoryRequirements(api.m_device, m_buffer, &memoryReqs);

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
    if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
    {
        flagInfo.deviceMask = 1;
        flagInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

        flagInfo.pNext = allocateInfo.pNext;
        allocateInfo.pNext = &flagInfo;
    }

    SLANG_VK_RETURN_ON_FAIL(api.vkAllocateMemory(api.m_device, &allocateInfo, nullptr, &m_memory));
    SLANG_VK_RETURN_ON_FAIL(api.vkBindBufferMemory(api.m_device, m_buffer, m_memory, 0));

    return SLANG_OK;
}

BufferImpl::BufferImpl(DeviceImpl* device, const BufferDesc& desc)
    : Buffer(desc)
    , m_device(device)
{
}

BufferImpl::~BufferImpl()
{
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

DeviceAddress BufferImpl::getDeviceAddress()
{
    if (!m_buffer.m_api->vkGetBufferDeviceAddress)
        return 0;
    VkBufferDeviceAddressInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    info.buffer = m_buffer.m_buffer;
    return (DeviceAddress)m_buffer.m_api->vkGetBufferDeviceAddress(m_buffer.m_api->m_device, &info);
}

Result BufferImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::VkBuffer;
    outHandle->value = (uint64_t)m_buffer.m_buffer;
    return SLANG_OK;
}

Result BufferImpl::getSharedHandle(NativeHandle* outHandle)
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
    info.memory = m_buffer.m_memory;
    info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;

    auto api = m_buffer.m_api;
    PFN_vkGetMemoryWin32HandleKHR vkCreateSharedHandle;
    vkCreateSharedHandle = api->vkGetMemoryWin32HandleKHR;
    if (!vkCreateSharedHandle)
    {
        return SLANG_FAIL;
    }
    SLANG_VK_RETURN_ON_FAIL(vkCreateSharedHandle(api->m_device, &info, (HANDLE*)&m_sharedHandle.value));
    m_sharedHandle.type = NativeHandleType::Win32;
#else
    VkMemoryGetFdInfoKHR info = {};
    info.sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR;
    info.pNext = nullptr;
    info.memory = m_buffer.m_memory;
    info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;

    auto api = m_buffer.m_api;
    PFN_vkGetMemoryFdKHR vkCreateSharedHandle;
    vkCreateSharedHandle = api->vkGetMemoryFdKHR;
    if (!vkCreateSharedHandle)
    {
        return SLANG_FAIL;
    }
    SLANG_VK_RETURN_ON_FAIL(vkCreateSharedHandle(api->m_device, &info, (int*)&m_sharedHandle.value));
    m_sharedHandle.type = NativeHandleType::FileDescriptor;
#endif
    *outHandle = m_sharedHandle;
    return SLANG_OK;
}

VkBufferView BufferImpl::getView(Format format, const BufferRange& range)
{
    ViewKey key = {format, range};

    std::lock_guard<std::mutex> lock(m_mutex);

    VkBufferView& view = m_views[key];
    if (view)
        return view;

    VkBufferViewCreateInfo info = {VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO};
    info.format = VulkanUtil::getVkFormat(format);
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

Result DeviceImpl::createBuffer(const BufferDesc& descIn, const void* initData, IBuffer** outBuffer)
{
    BufferDesc desc = fixupBufferDesc(descIn);

    const Size bufferSize = desc.size;

    VkMemoryPropertyFlags reqMemoryProperties = 0;

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

    if (is_set(desc.usage, BufferUsage::ConstantBuffer) || desc.memoryType == MemoryType::Upload ||
        desc.memoryType == MemoryType::ReadBack)
    {
        reqMemoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    }
    else
    {
        reqMemoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    }

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
            buffer->m_buffer.init(m_api, desc.size, usage, reqMemoryProperties, externalMemoryHandleTypeFlags)
        );
    }
    else
    {
        SLANG_RETURN_ON_FAIL(buffer->m_buffer.init(m_api, desc.size, usage, reqMemoryProperties));
    }

    _labelObject((uint64_t)buffer->m_buffer.m_buffer, VK_OBJECT_TYPE_BUFFER, desc.label);

    if (initData)
    {
        if (desc.memoryType == MemoryType::DeviceLocal)
        {
            SLANG_RETURN_ON_FAIL(buffer->m_uploadBuffer.init(
                m_api,
                bufferSize,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
            ));
            // Copy into staging buffer
            void* mappedData = nullptr;
            SLANG_VK_CHECK(m_api.vkMapMemory(m_device, buffer->m_uploadBuffer.m_memory, 0, bufferSize, 0, &mappedData));
            ::memcpy(mappedData, initData, bufferSize);
            m_api.vkUnmapMemory(m_device, buffer->m_uploadBuffer.m_memory);

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
            SLANG_VK_CHECK(m_api.vkMapMemory(m_device, buffer->m_buffer.m_memory, 0, bufferSize, 0, &mappedData));
            ::memcpy(mappedData, initData, bufferSize);
            m_api.vkUnmapMemory(m_device, buffer->m_buffer.m_memory);
        }
    }

    returnComPtr(outBuffer, buffer);
    return SLANG_OK;
}

Result DeviceImpl::createBufferFromNativeHandle(NativeHandle handle, const BufferDesc& srcDesc, IBuffer** outBuffer)
{
    RefPtr<BufferImpl> buffer(new BufferImpl(this, srcDesc));

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
    SLANG_VK_RETURN_ON_FAIL(
        m_api.vkMapMemory(m_api.m_device, bufferImpl->m_buffer.m_memory, 0, VK_WHOLE_SIZE, 0, outData)
    );
    return SLANG_OK;
}

Result DeviceImpl::unmapBuffer(IBuffer* buffer)
{
    BufferImpl* bufferImpl = checked_cast<BufferImpl*>(buffer);
    m_api.vkUnmapMemory(m_api.m_device, bufferImpl->m_buffer.m_memory);
    return SLANG_OK;
}

} // namespace rhi::vk
