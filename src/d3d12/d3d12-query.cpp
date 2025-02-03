#include "d3d12-query.h"
#include "d3d12-device.h"
#include "d3d12-command.h"
#include "d3d12-buffer.h"
#include "d3d12-helper-functions.h"

namespace rhi::d3d12 {

Result QueryPoolImpl::init(const QueryPoolDesc& desc, DeviceImpl* device)
{
    m_desc = desc;

    // Translate query type.
    D3D12_QUERY_HEAP_DESC heapDesc = {};
    heapDesc.Count = (UINT)desc.count;
    heapDesc.NodeMask = 1;
    switch (desc.type)
    {
    case QueryType::Timestamp:
        heapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        m_queryType = D3D12_QUERY_TYPE_TIMESTAMP;
        break;
    default:
        return SLANG_E_INVALID_ARG;
    }

    // Create query heap.
    auto d3dDevice = device->m_device;
    SLANG_RETURN_ON_FAIL(d3dDevice->CreateQueryHeap(&heapDesc, IID_PPV_ARGS(m_queryHeap.writeRef())));

    if (desc.label)
    {
        m_queryHeap->SetName(string::to_wstring(desc.label).c_str());
    }

    // Create readback buffer.
    D3D12_HEAP_PROPERTIES heapProps;
    heapProps.Type = D3D12_HEAP_TYPE_READBACK;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;
    D3D12_RESOURCE_DESC resourceDesc = {};
    initBufferDesc(sizeof(uint64_t) * desc.count, resourceDesc);
    SLANG_RETURN_ON_FAIL(m_readBackBuffer.initCommitted(
        d3dDevice,
        heapProps,
        D3D12_HEAP_FLAG_NONE,
        resourceDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr
    ));

    // Create command allocator.
    SLANG_RETURN_ON_FAIL(
        d3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(m_commandAllocator.writeRef()))
    );

    // Create command list.
    SLANG_RETURN_ON_FAIL(d3dDevice->CreateCommandList(
        0,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        m_commandAllocator,
        nullptr,
        IID_PPV_ARGS(m_commandList.writeRef())
    ));
    m_commandList->Close();

    // Create fence.
    SLANG_RETURN_ON_FAIL(d3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(m_fence.writeRef())));

    // Get command queue from device.
    m_commandQueue = device->m_queue->m_d3dQueue;

    // Create wait event.
    m_waitEvent = CreateEventEx(nullptr, FALSE, 0, EVENT_ALL_ACCESS);

    return SLANG_OK;
}

Result QueryPoolImpl::getResult(uint32_t queryIndex, uint32_t count, uint64_t* data)
{
    m_commandList->Reset(m_commandAllocator, nullptr);
    m_commandList->ResolveQueryData(
        m_queryHeap,
        m_queryType,
        (UINT)queryIndex,
        (UINT)count,
        m_readBackBuffer,
        sizeof(uint64_t) * queryIndex
    );
    m_commandList->Close();
    ID3D12CommandList* cmdList = m_commandList;
    m_commandQueue->ExecuteCommandLists(1, &cmdList);
    m_eventValue++;
    m_fence->SetEventOnCompletion(m_eventValue, m_waitEvent);
    m_commandQueue->Signal(m_fence, m_eventValue);
    WaitForSingleObject(m_waitEvent, INFINITE);
    m_commandAllocator->Reset();

    int8_t* mappedData = nullptr;
    D3D12_RANGE readRange = {sizeof(uint64_t) * queryIndex, sizeof(uint64_t) * (queryIndex + count)};
    m_readBackBuffer.getResource()->Map(0, &readRange, (void**)&mappedData);
    memcpy(data, mappedData + sizeof(uint64_t) * queryIndex, sizeof(uint64_t) * count);
    m_readBackBuffer.getResource()->Unmap(0, nullptr);
    return SLANG_OK;
}

void QueryPoolImpl::writeTimestamp(ID3D12GraphicsCommandList* cmdList, uint32_t index)
{
    cmdList->EndQuery(m_queryHeap, D3D12_QUERY_TYPE_TIMESTAMP, index);
}

IQueryPool* PlainBufferProxyQueryPoolImpl::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IQueryPool::getTypeGuid())
        return static_cast<IQueryPool*>(this);
    return nullptr;
}

Result PlainBufferProxyQueryPoolImpl::init(const QueryPoolDesc& desc, DeviceImpl* device, uint32_t stride)
{
    ComPtr<IBuffer> buffer;
    BufferDesc bufferDesc = {};
    bufferDesc.defaultState = ResourceState::CopySource;
    bufferDesc.elementSize = 0;
    bufferDesc.size = desc.count * stride;
    bufferDesc.format = Format::Unknown;
    bufferDesc.usage = BufferUsage::UnorderedAccess;
    SLANG_RETURN_ON_FAIL(device->createBuffer(bufferDesc, nullptr, buffer.writeRef()));
    m_buffer = checked_cast<BufferImpl*>(buffer.get());
    m_queryType = desc.type;
    m_device = device;
    m_stride = stride;
    m_count = (uint32_t)desc.count;
    m_desc = desc;
    return SLANG_OK;
}

Result PlainBufferProxyQueryPoolImpl::reset()
{
    m_resultDirty = true;
    ID3D12GraphicsCommandList* commandList = m_device->beginImmediateCommandList();
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barrier.Transition.pResource = m_buffer->m_resource.getResource();
    commandList->ResourceBarrier(1, &barrier);
    m_device->endImmediateCommandList();
    return SLANG_OK;
}

Result PlainBufferProxyQueryPoolImpl::getResult(uint32_t queryIndex, uint32_t count, uint64_t* data)
{
    if (m_resultDirty)
    {
        ID3D12GraphicsCommandList* commandList = m_device->beginImmediateCommandList();
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        barrier.Transition.pResource = m_buffer->m_resource.getResource();
        commandList->ResourceBarrier(1, &barrier);

        D3D12Resource stageBuf;

        uint64_t size = uint64_t(m_count) * m_stride;
        D3D12_HEAP_PROPERTIES heapProps;
        heapProps.Type = D3D12_HEAP_TYPE_READBACK;
        heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heapProps.CreationNodeMask = 1;
        heapProps.VisibleNodeMask = 1;

        D3D12_RESOURCE_DESC stagingDesc;
        initBufferDesc(size, stagingDesc);

        SLANG_RETURN_ON_FAIL(stageBuf.initCommitted(
            m_device->m_device,
            heapProps,
            D3D12_HEAP_FLAG_NONE,
            stagingDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr
        ));

        commandList->CopyBufferRegion(stageBuf, 0, m_buffer->m_resource.getResource(), 0, size);
        m_device->endImmediateCommandList();
        void* ptr = nullptr;
        stageBuf.getResource()->Map(0, nullptr, &ptr);
        m_result.resize(m_count * m_stride);
        memcpy(m_result.data(), ptr, m_result.size());

        m_resultDirty = false;
    }

    memcpy(data, m_result.data() + queryIndex * m_stride, count * m_stride);

    return SLANG_OK;
}

} // namespace rhi::d3d12
