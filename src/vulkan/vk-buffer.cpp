#include "vk-buffer.h"
#include "vk-device.h"
#include "vk-resource-heap.h"
#include "vk-utils.h"

#if !SLANG_WINDOWS_FAMILY
#include <unistd.h>
#endif

namespace rhi::vk {

Result createVkBuffer(const VulkanApi& api, Size size, VkBufferUsageFlags usage, VkBuffer* outBuffer)
{
    VkBufferCreateInfo createInfo = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    createInfo.size = size;
    createInfo.usage = usage;
    createInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    SLANG_VK_RETURN_ON_FAIL(api.vkCreateBuffer(api.m_device, &createInfo, nullptr, outBuffer));
    return SLANG_OK;
}

Result allocateVkBuffer(
    DeviceImpl* device,
    Size size,
    VkBufferUsageFlags usage,
    const AllocationDesc& allocationDesc,
    VkBuffer* outBuffer,
    MemoryAllocation& outAllocation
)
{
    VkBufferCreateInfo createInfo = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    createInfo.size = size;
    createInfo.usage = usage;
    createInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    // Buffers are sub-allocated from larger memory blocks, so they don't start at offset 0.
    // Buffers with a device address need an explicit alignment that covers all their uses.
    const VkDeviceSize alignment = getBufferDeviceAddressAlignment(device->m_api, usage);
    return device->m_memoryAllocator.createBuffer(createInfo, allocationDesc, alignment, outBuffer, outAllocation);
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

    // The buffer must be destroyed before its memory (m_allocation) is freed.
    if (m_ownsBuffer && m_buffer != VK_NULL_HANDLE)
    {
        device->m_api.vkDestroyBuffer(device->m_api.m_device, m_buffer, nullptr);
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
    outHandle->value = (uint64_t)m_buffer;
    return SLANG_OK;
}

Result BufferImpl::getSharedHandle(NativeHandle* outHandle)
{
    // Only buffers created with BufferUsage::Shared own exportable memory. All other buffers are
    // sub-allocated from memory blocks shared with unrelated resources (or placed in a resource heap).
    if (!m_sharedHandle)
    {
        SLANG_RETURN_ON_FAIL(getDevice<DeviceImpl>()->m_memoryAllocator.exportHandle(m_allocation, &m_sharedHandle));
    }
    *outHandle = m_sharedHandle;
    return SLANG_OK;
}

void* BufferImpl::getMappedData() const
{
    if (isPlaced())
    {
        ResourceHeapImpl* heap = checked_cast<ResourceHeapImpl*>(m_placement->heap.get());
        uint8_t* heapData = static_cast<uint8_t*>(heap->m_allocation.getMappedData());
        return heapData ? heapData + m_placement->offset : nullptr;
    }
    return m_allocation.getMappedData();
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
        info.buffer = m_buffer;
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
    info.buffer = m_buffer;
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
    m_api.vkCmdCopyBuffer(commandBuffer, srcBuffer->m_buffer, dstBuffer->m_buffer, 1, &copy);

    VkBufferMemoryBarrier dstBarrier = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    dstBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    dstBarrier.dstAccessMask = calcAccessFlags(dstBuffer->m_desc.defaultState);
    dstBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    dstBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    dstBarrier.buffer = dstBuffer->m_buffer;
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

    RefPtr<BufferImpl> buffer(new BufferImpl(this, desc));
    if (desc.heap)
    {
        ResourceMemoryRequirements requirements = {};
        SLANG_RETURN_ON_FAIL(getBufferMemoryRequirements(desc, &requirements));
        SLANG_RETURN_ON_FAIL(validateResourcePlacement(this, desc.heap, desc.heapOffset, requirements));

        // Placed buffer: bind the buffer to memory owned by the resource heap.
        ResourceHeapImpl* heap = checked_cast<ResourceHeapImpl*>(desc.heap);
        SLANG_RETURN_ON_FAIL(createVkBuffer(m_api, desc.size, usage, &buffer->m_buffer));
        VkMemoryRequirements memoryRequirements = {};
        m_api.vkGetBufferMemoryRequirements(m_device, buffer->m_buffer, &memoryRequirements);
        if ((memoryRequirements.memoryTypeBits & (1u << heap->m_allocation.getMemoryTypeIndex())) == 0)
            return SLANG_E_INVALID_ARG;
        SLANG_VK_RETURN_ON_FAIL_REPORT(
            m_api.vkBindBufferMemory(
                m_device,
                buffer->m_buffer,
                heap->m_allocation.getDeviceMemory(),
                heap->m_allocation.getOffset() + desc.heapOffset
            ),
            this
        );
        buffer->setPlacement(heap, desc.heapOffset, requirements.size);
    }
    else
    {
        AllocationDesc allocationDesc;
        allocationDesc.memoryType = desc.memoryType;
        // Constant buffers are always host visible.
        if (is_set(desc.usage, BufferUsage::ConstantBuffer) && desc.memoryType == MemoryType::DeviceLocal)
            allocationDesc.memoryType = MemoryType::Upload;
        allocationDesc.exportable = is_set(desc.usage, BufferUsage::Shared);
        SLANG_RETURN_ON_FAIL(
            allocateVkBuffer(this, desc.size, usage, allocationDesc, &buffer->m_buffer, buffer->m_allocation)
        );
    }

    _labelObject((uint64_t)buffer->m_buffer, VK_OBJECT_TYPE_BUFFER, desc.label);

    if (initData)
    {
        if (desc.memoryType == MemoryType::DeviceLocal)
        {
            SLANG_RETURN_ON_FAIL(uploadBufferInitData(buffer, 0, bufferSize, initData));
        }
        else
        {
            void* mappedData = buffer->getMappedData();
            if (!mappedData)
                return SLANG_FAIL;
            ::memcpy(mappedData, initData, bufferSize);
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
    buffer->m_buffer = (VkBuffer)handle.value;
    buffer->m_ownsBuffer = false;

    returnComPtr(outBuffer, buffer);
    return SLANG_OK;
}

Result DeviceImpl::mapBuffer(IBuffer* buffer, CpuAccessMode mode, void** outData)
{
    SLANG_UNUSED(mode);
    BufferImpl* bufferImpl = checked_cast<BufferImpl*>(buffer);
    // Buffers created from native handles have no memory we can map.
    if (!bufferImpl->isPlaced() && !bufferImpl->m_allocation)
        return SLANG_E_NOT_AVAILABLE;
    // Upload and ReadBack memory is persistently mapped, DeviceLocal memory can't be mapped.
    // Note that constant buffers are always allocated as Upload memory.
    void* mappedData = bufferImpl->getMappedData();
    if (!mappedData)
        return SLANG_E_INVALID_ARG;
    *outData = mappedData;
    return SLANG_OK;
}

Result DeviceImpl::unmapBuffer(IBuffer* buffer)
{
    BufferImpl* bufferImpl = checked_cast<BufferImpl*>(buffer);
    if (!bufferImpl->isPlaced() && !bufferImpl->m_allocation)
        return SLANG_E_NOT_AVAILABLE;
    // Host visible memory is persistently mapped, so there is nothing to unmap.
    return SLANG_OK;
}

} // namespace rhi::vk
