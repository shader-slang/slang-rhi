#include "vk-buffer.h"
#include "vk-device.h"
#include "vk-resource-heap.h"
#include "vk-utils.h"

#if SLANG_WINDOWS_FAMILY
#include <dxgi1_4.h>
#endif

#if !SLANG_WINDOWS_FAMILY
#include <unistd.h>
#endif


namespace rhi::vk {

VkBufferUsageFlags getBufferUsageFlags(const DeviceImpl* device, const BufferDesc& desc)
{
    VkBufferUsageFlags usage = _calcBufferUsageFlags(desc.usage) | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (device->m_api.m_extendedFeatures.vulkan12Features.bufferDeviceAddress)
        usage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    if (is_set(desc.usage, BufferUsage::ShaderResource) &&
        device->m_api.m_extendedFeatures.accelerationStructureFeatures.accelerationStructure)
    {
        usage |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
    }
    return usage;
}

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
    VkMemoryPropertyFlags reqMemoryProperties,
    VkExternalMemoryHandleTypeFlagsKHR externalMemoryHandleTypeFlags
)
{
    SLANG_RHI_ASSERT(!isInitialized());

    m_api = &api;
    m_memory = VK_NULL_HANDLE;
    m_buffer = VK_NULL_HANDLE;

    // Create buffer
    SLANG_RETURN_ON_FAIL(createVkBuffer(api, bufferSize, usage, externalMemoryHandleTypeFlags, &m_buffer));

    // Allocate memory
    SLANG_RETURN_ON_FAIL(
        allocateVkMemoryForBuffer(api, m_buffer, usage, reqMemoryProperties, externalMemoryHandleTypeFlags, &m_memory)
    );

    // Bind buffer to memory
    SLANG_VK_RETURN_ON_FAIL(api.vkBindBufferMemory(api.m_device, m_buffer, m_memory, 0));
    m_ownsMemory = true;

    return SLANG_OK;
}

Result VKBufferHandleRAII::initPlaced(
    const VulkanApi& api,
    Size bufferSize,
    VkBufferUsageFlags usage,
    uint32_t memoryTypeIndex,
    VkDeviceMemory memory,
    Offset offset
)
{
    SLANG_RHI_ASSERT(!isInitialized());

    m_api = &api;
    m_memory = memory;
    m_buffer = VK_NULL_HANDLE;
    m_ownsMemory = false;

    SLANG_RETURN_ON_FAIL(createVkBuffer(api, bufferSize, usage, 0, &m_buffer));
    VkMemoryRequirements requirements = {};
    api.vkGetBufferMemoryRequirements(api.m_device, m_buffer, &requirements);
    if ((requirements.memoryTypeBits & (1u << memoryTypeIndex)) == 0)
        return SLANG_E_INVALID_ARG;
    SLANG_VK_RETURN_ON_FAIL(api.vkBindBufferMemory(api.m_device, m_buffer, memory, offset));
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
        SLANG_VK_RETURN_ON_FAIL_REPORT(api.vkGetMemoryWin32HandleKHR(api.m_device, &info, &handle), device);
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
        SLANG_VK_RETURN_ON_FAIL_REPORT(api.vkGetMemoryFdKHR(api.m_device, &info, &handle), device);
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

Result DeviceImpl::uploadBufferInitData(IBuffer* buffer, Offset offset, Size size, const void* data)
{
    if (!buffer || !data || size == 0)
        return SLANG_E_INVALID_ARG;

    BufferImpl* dstBuffer = checked_cast<BufferImpl*>(buffer);
    if (offset > dstBuffer->m_desc.size || size > dstBuffer->m_desc.size - offset)
        return SLANG_E_INVALID_ARG;

    RefPtr<StagingHeap::Handle> stagingHandle;
    // Vulkan buffer copy offsets must be aligned to four bytes.
    SLANG_RETURN_ON_FAIL(m_uploadHeap.stageHandle(data, size, 4, {}, stagingHandle.writeRef()));
    BufferImpl* srcBuffer = checked_cast<BufferImpl*>(stagingHandle->getBuffer());

    VkCommandBuffer commandBuffer = m_deviceQueue.getCommandBuffer();

    VkBufferCopy copy = {};
    copy.srcOffset = stagingHandle->getOffset();
    copy.dstOffset = offset;
    copy.size = size;
    m_api.vkCmdCopyBuffer(commandBuffer, srcBuffer->m_buffer.m_buffer, dstBuffer->m_buffer.m_buffer, 1, &copy);

    VkBufferMemoryBarrier dstBarrier = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    dstBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    dstBarrier.dstAccessMask = calcAccessFlags(dstBuffer->m_desc.defaultState);
    dstBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    dstBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    dstBarrier.buffer = dstBuffer->m_buffer.m_buffer;
    dstBarrier.offset = offset;
    dstBarrier.size = size;
    m_api.vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        calcPipelineStageFlags(m_api.m_supportedShaderStageFlags, dstBuffer->m_desc.defaultState, false),
        0,
        0,
        nullptr,
        1,
        &dstBarrier,
        0,
        nullptr
    );

    // Both buffers must outlive the asynchronous copy. The staging handle also
    // returns its allocation to the shared heap when this fence slot retires.
    m_deviceQueue.retainResource(stagingHandle);
    m_deviceQueue.retainResource(dstBuffer);
    m_deviceQueue.flush();

    return SLANG_OK;
}

Result DeviceImpl::createBuffer(const BufferDesc& desc_, const void* initData, IBuffer** outBuffer)
{
    BufferDesc desc = fixupBufferDesc(desc_);

    const Size bufferSize = desc.size;

    VkMemoryPropertyFlags reqMemoryProperties = 0;

    VkBufferUsageFlags usage = getBufferUsageFlags(this, desc);

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
    const ResourcePlacementDesc* placement = findResourcePlacementDesc(desc.next);
    if (placement)
    {
        ResourceMemoryRequirements requirements = {};
        SLANG_RETURN_ON_FAIL(getBufferMemoryRequirements(desc, &requirements));
        SLANG_RETURN_ON_FAIL(validateResourcePlacement(this, *placement, requirements));

        ResourceHeapImpl* heap = checked_cast<ResourceHeapImpl*>(placement->heap);
        SLANG_RETURN_ON_FAIL(
            buffer->m_buffer
                .initPlaced(m_api, desc.size, usage, heap->m_memoryTypeIndex, heap->m_memory, placement->offset)
        );
        buffer->m_resourceHeap = heap;
        buffer->m_resourceHeapOffset = placement->offset;
    }
    else if (is_set(desc.usage, BufferUsage::Shared))
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
            SLANG_RETURN_ON_FAIL(uploadBufferInitData(buffer, 0, bufferSize, initData));
        }
        else
        {
            void* mappedData = nullptr;
            if (placement)
            {
                ResourceHeapImpl* heap = checked_cast<ResourceHeapImpl*>(placement->heap);
                if (!heap->m_mapped)
                    return SLANG_FAIL;
                mappedData = static_cast<uint8_t*>(heap->m_mapped) + placement->offset;
                ::memcpy(mappedData, initData, bufferSize);
            }
            else
            {
                SLANG_VK_RETURN_ON_FAIL_REPORT(
                    m_api.vkMapMemory(m_device, buffer->m_buffer.m_memory, 0, bufferSize, 0, &mappedData),
                    this
                );
                ::memcpy(mappedData, initData, bufferSize);
                m_api.vkUnmapMemory(m_device, buffer->m_buffer.m_memory);
            }
        }
    }

    returnComPtr(outBuffer, buffer);
    return SLANG_OK;
}

Result DeviceImpl::createBufferFromNativeHandle(NativeHandle handle, const BufferDesc& desc, IBuffer** outBuffer)
{
    if (handle.type != NativeHandleType::VkBuffer || handle.value == 0)
    {
        *outBuffer = nullptr;
        return SLANG_E_INVALID_HANDLE;
    }

    RefPtr<BufferImpl> buffer(new BufferImpl(this, fixupBufferDesc(desc)));
    buffer->m_buffer.m_buffer = (VkBuffer)handle.value;

    returnComPtr(outBuffer, buffer);
    return SLANG_OK;
}

Result DeviceImpl::mapBuffer(IBuffer* buffer, CpuAccessMode mode, void** outData)
{
    BufferImpl* bufferImpl = checked_cast<BufferImpl*>(buffer);
    if (bufferImpl->m_resourceHeap)
    {
        if (!bufferImpl->m_resourceHeap->m_mapped)
            return SLANG_FAIL;
        *outData = static_cast<uint8_t*>(bufferImpl->m_resourceHeap->m_mapped) + bufferImpl->m_resourceHeapOffset;
        return SLANG_OK;
    }
    SLANG_VK_RETURN_ON_FAIL_REPORT(
        m_api.vkMapMemory(m_api.m_device, bufferImpl->m_buffer.m_memory, 0, VK_WHOLE_SIZE, 0, outData),
        this
    );
    return SLANG_OK;
}

Result DeviceImpl::unmapBuffer(IBuffer* buffer)
{
    BufferImpl* bufferImpl = checked_cast<BufferImpl*>(buffer);
    if (bufferImpl->m_resourceHeap)
        return SLANG_OK;
    m_api.vkUnmapMemory(m_api.m_device, bufferImpl->m_buffer.m_memory);
    return SLANG_OK;
}

} // namespace rhi::vk
