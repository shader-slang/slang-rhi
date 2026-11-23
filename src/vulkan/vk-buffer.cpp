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

Result VKBufferHandleRAII::init(
    DeviceImpl* device,
    Size bufferSize,
    VkBufferUsageFlags usage,
    MemoryType memoryType,
    bool exportable
)
{
    SLANG_RHI_ASSERT(!isInitialized());

    const VulkanApi& api = device->m_api;
    VmaAllocator vmaAllocator = device->m_vmaAllocator;

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
        // Readback memory is read by the CPU, so prefer a cached memory type.
        // Uncached (write-combined) memory is very slow to read from the CPU.
        allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
        allocCreateInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
        allocCreateInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        allocCreateInfo.preferredFlags = VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
        break;
    }

    // Exportable buffers are allocated from a pool that creates exportable memory.
    // Use dedicated memory so each buffer gets its own VkDeviceMemory that can be
    // exported via OS handles.
    VkExternalMemoryBufferCreateInfo externalMemoryBufferCreateInfo = {
        VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO
    };
    if (exportable)
    {
        externalMemoryBufferCreateInfo.handleTypes = DeviceImpl::kSharedMemoryHandleType;
        bufferCreateInfo.pNext = &externalMemoryBufferCreateInfo;

        uint32_t memoryTypeIndex = 0;
        SLANG_VK_RETURN_ON_FAIL(
            vmaFindMemoryTypeIndexForBufferInfo(vmaAllocator, &bufferCreateInfo, &allocCreateInfo, &memoryTypeIndex)
        );
        SLANG_RETURN_ON_FAIL(device->getExportPool(memoryTypeIndex, &allocCreateInfo.pool));
        allocCreateInfo.flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
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

    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation vmaAllocation = VK_NULL_HANDLE;
    SLANG_VK_RETURN_ON_FAIL(vmaCreateBufferWithAlignment(
        vmaAllocator,
        &bufferCreateInfo,
        &allocCreateInfo,
        minAlignment,
        &buffer,
        &vmaAllocation,
        nullptr
    ));

    // Retrieve the VkDeviceMemory for exporting shared buffers.
    // NOTE: Unless the allocation is dedicated, this memory is shared with other
    // allocations and must never be used directly without the allocation offset.
    VmaAllocationInfo allocInfo;
    vmaGetAllocationInfo(vmaAllocator, vmaAllocation, &allocInfo);
    m_api = &api;
    m_buffer = buffer;
    m_vmaAllocator = vmaAllocator;
    m_vmaAllocation = vmaAllocation;
    m_memory = allocInfo.deviceMemory;

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

    SLANG_RETURN_ON_FAIL(createVkBuffer(api, bufferSize, usage, 0, &m_buffer));
    m_ownsBuffer = true;
    VkMemoryRequirements requirements = {};
    api.vkGetBufferMemoryRequirements(api.m_device, m_buffer, &requirements);
    if ((requirements.memoryTypeBits & (1u << memoryTypeIndex)) == 0)
        return SLANG_E_INVALID_ARG;
    SLANG_VK_RETURN_ON_FAIL(api.vkBindBufferMemory(api.m_device, m_buffer, memory, offset));
    return SLANG_OK;
}

void VKBufferHandleRAII::initFromNativeHandle(const VulkanApi& api, VkBuffer buffer)
{
    SLANG_RHI_ASSERT(!isInitialized());
    SLANG_RHI_ASSERT(buffer != VK_NULL_HANDLE);

    m_api = &api;
    m_buffer = buffer;
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
        device->m_api.vkDestroyBufferView(device->m_api.m_device, view.second, nullptr);
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
    // Shared buffers use dedicated allocations from an export pool, so m_memory
    // is the exclusive backing memory and can be exported directly.
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

    DeviceImpl* device = getDevice<DeviceImpl>();
    VkResult result = device->m_api.vkCreateBufferView(device->m_api.m_device, &info, nullptr, &view);
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

    VkBufferUsageFlags usage = getBufferUsageFlags(m_api, desc);

    // Constant buffers are always host visible.
    MemoryType effectiveMemoryType = desc.memoryType;
    if (is_set(desc.usage, BufferUsage::ConstantBuffer) && desc.memoryType == MemoryType::DeviceLocal)
        effectiveMemoryType = MemoryType::Upload;

    RefPtr<BufferImpl> buffer(new BufferImpl(this, desc));
    if (desc.heap)
    {
        ResourceMemoryRequirements requirements = {};
        SLANG_RETURN_ON_FAIL(getBufferMemoryRequirements(desc, &requirements));
        SLANG_RETURN_ON_FAIL(validateResourcePlacement(this, desc.heap, desc.heapOffset, requirements));

        ResourceHeapImpl* heap = checked_cast<ResourceHeapImpl*>(desc.heap);
        SLANG_RETURN_ON_FAIL(
            buffer->m_buffer
                .initPlaced(m_api, desc.size, usage, heap->m_memoryTypeIndex, heap->m_memory, desc.heapOffset)
        );
        buffer->setPlacement(heap, desc.heapOffset, requirements.size);
    }
    else
    {
        const bool exportable = is_set(desc.usage, BufferUsage::Shared);
        SLANG_RETURN_ON_FAIL(buffer->m_buffer.init(this, desc.size, usage, effectiveMemoryType, exportable));
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
            if (desc.heap)
            {
                ResourceHeapImpl* heap = checked_cast<ResourceHeapImpl*>(desc.heap);
                if (!heap->m_mapped)
                    return SLANG_FAIL;
                mappedData = static_cast<uint8_t*>(heap->m_mapped) + desc.heapOffset;
                ::memcpy(mappedData, initData, bufferSize);
            }
            else
            {
                SLANG_VK_RETURN_ON_FAIL_REPORT(
                    vmaMapMemory(m_vmaAllocator, buffer->m_buffer.m_vmaAllocation, &mappedData),
                    this
                );
                ::memcpy(mappedData, initData, bufferSize);
                vmaUnmapMemory(m_vmaAllocator, buffer->m_buffer.m_vmaAllocation);
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
    buffer->m_buffer.initFromNativeHandle(m_api, (VkBuffer)handle.value);

    returnComPtr(outBuffer, buffer);
    return SLANG_OK;
}

Result DeviceImpl::mapBuffer(IBuffer* buffer, CpuAccessMode mode, void** outData)
{
    BufferImpl* bufferImpl = checked_cast<BufferImpl*>(buffer);
    if (bufferImpl->isPlaced())
    {
        ResourceHeapImpl* heap = checked_cast<ResourceHeapImpl*>(bufferImpl->m_placement->heap.get());
        if (!heap->m_mapped)
            return SLANG_FAIL;
        *outData = static_cast<uint8_t*>(heap->m_mapped) + bufferImpl->m_placement->offset;
        return SLANG_OK;
    }
    // Buffers created from native handles have no memory we can map.
    if (!bufferImpl->m_buffer.m_vmaAllocation)
        return SLANG_E_NOT_AVAILABLE;
    SLANG_VK_RETURN_ON_FAIL_REPORT(vmaMapMemory(m_vmaAllocator, bufferImpl->m_buffer.m_vmaAllocation, outData), this);
    return SLANG_OK;
}

Result DeviceImpl::unmapBuffer(IBuffer* buffer)
{
    BufferImpl* bufferImpl = checked_cast<BufferImpl*>(buffer);
    if (bufferImpl->isPlaced())
        return SLANG_OK;
    if (!bufferImpl->m_buffer.m_vmaAllocation)
        return SLANG_E_NOT_AVAILABLE;
    vmaUnmapMemory(m_vmaAllocator, bufferImpl->m_buffer.m_vmaAllocation);
    return SLANG_OK;
}

} // namespace rhi::vk
