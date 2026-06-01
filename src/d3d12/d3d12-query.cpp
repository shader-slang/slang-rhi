#include "d3d12-query.h"
#include "d3d12-device.h"
#include "d3d12-command.h"
#include "d3d12-buffer.h"
#include "d3d12-utils.h"

namespace rhi::d3d12 {

QueryPoolImpl::QueryPoolImpl(Device* device, const QueryPoolDesc& desc)
    : QueryPool(device, desc)
{
}

QueryPoolImpl::~QueryPoolImpl()
{
    if (m_mappedReadBackData)
    {
        D3D12_RANGE writtenRange = {0, 0};
        m_readBackBuffer.getResource()->Unmap(0, &writtenRange);
        m_mappedReadBackData = nullptr;
    }
}

Result QueryPoolImpl::init()
{
    DeviceImpl* device = getDevice<DeviceImpl>();

    // Translate query type.
    D3D12_QUERY_HEAP_DESC heapDesc = {};
    heapDesc.Count = (UINT)m_desc.count;
    heapDesc.NodeMask = 1;
    switch (m_desc.type)
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

    if (m_desc.label)
    {
        m_queryHeap->SetName(string::to_wstring(m_desc.label).c_str());
    }

    // Create readback buffer.
    D3D12_HEAP_PROPERTIES heapProps = makeHeapProperties(D3D12_HEAP_TYPE_READBACK);
    D3D12_RESOURCE_DESC resourceDesc = {};
    initBufferDesc(sizeof(uint64_t) * m_desc.count, resourceDesc);
    SLANG_RETURN_ON_FAIL(m_readBackBuffer.initCommitted(
        d3dDevice,
        heapProps,
        D3D12_HEAP_FLAG_NONE,
        resourceDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        device->m_allocator
    ));

    D3D12_RANGE readRange = {0, sizeof(uint64_t) * m_desc.count};
    SLANG_RETURN_ON_FAIL(
        m_readBackBuffer.getResource()->Map(0, &readRange, reinterpret_cast<void**>(&m_mappedReadBackData))
    );

    return SLANG_OK;
}

Result QueryPoolImpl::isResultReady(uint32_t queryIndex, uint32_t count, bool* outReady)
{
    if (!outReady || !isValidQueryRange(queryIndex, count))
    {
        return SLANG_E_INVALID_ARG;
    }

    *outReady = false;
    QueryRangeInfo queryInfo = getQueryRangeInfo(queryIndex, count);
    if (queryInfo.state == QueryRangeState::Reset)
    {
        return SLANG_FAIL;
    }
    if (queryInfo.state == QueryRangeState::Resolved)
    {
        *outReady = true;
        return SLANG_OK;
    }

    CommandQueueImpl* queue = getDevice<DeviceImpl>()->m_queue.get();
    uint64_t submissionID = queryInfo.submissionID;
    if (queue->updateLastFinishedID() < submissionID)
    {
        return SLANG_OK;
    }

    markQueryRangeReady(queryIndex, count, queryInfo.submissionID);
    *outReady = true;

    return SLANG_OK;
}

Result QueryPoolImpl::getResult(uint32_t queryIndex, uint32_t count, uint64_t* outData)
{
    if (!outData || !isValidQueryRange(queryIndex, count))
    {
        return SLANG_E_INVALID_ARG;
    }

    QueryRangeInfo queryInfo = getQueryRangeInfo(queryIndex, count);
    if (queryInfo.state == QueryRangeState::Reset)
    {
        return SLANG_FAIL;
    }
    if (count == 0)
    {
        return SLANG_OK;
    }

    CommandQueueImpl* queue = getDevice<DeviceImpl>()->m_queue.get();
    uint64_t submissionID = queryInfo.submissionID;
    if (queue->updateLastFinishedID() < submissionID)
    {
        ResetEvent(queue->m_globalWaitHandle);
        SLANG_RETURN_ON_FAIL(queue->m_trackingFence->SetEventOnCompletion(submissionID, queue->m_globalWaitHandle));
        WaitForSingleObject(queue->m_globalWaitHandle, INFINITE);
        queue->updateLastFinishedID();
        queue->retireCommandBuffers();
    }

    memcpy(outData, m_mappedReadBackData + sizeof(uint64_t) * queryIndex, sizeof(uint64_t) * count);
    markQueryRangeReady(queryIndex, count, queryInfo.submissionID);

    return SLANG_OK;
}

void QueryPoolImpl::writeTimestamp(ID3D12GraphicsCommandList* cmdList, uint32_t index)
{
    cmdList->EndQuery(m_queryHeap, D3D12_QUERY_TYPE_TIMESTAMP, index);
    cmdList->ResolveQueryData(m_queryHeap, m_queryType, index, 1, m_readBackBuffer, sizeof(uint64_t) * index);
}

IQueryPool* PlainBufferProxyQueryPoolImpl::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IQueryPool::getTypeGuid())
        return static_cast<IQueryPool*>(this);
    return nullptr;
}

PlainBufferProxyQueryPoolImpl::PlainBufferProxyQueryPoolImpl(Device* device, const QueryPoolDesc& desc)
    : QueryPool(device, desc)
{
}

PlainBufferProxyQueryPoolImpl::~PlainBufferProxyQueryPoolImpl()
{
    if (m_mappedReadBackData)
    {
        D3D12_RANGE writtenRange = {0, 0};
        m_readBackBuffer.getResource()->Unmap(0, &writtenRange);
        m_mappedReadBackData = nullptr;
    }
}

Result PlainBufferProxyQueryPoolImpl::init(uint32_t stride)
{
    DeviceImpl* device = getDevice<DeviceImpl>();
    uint64_t size = uint64_t(m_desc.count) * stride;

    ComPtr<IBuffer> buffer;
    BufferDesc bufferDesc = {};
    bufferDesc.defaultState = ResourceState::CopySource;
    bufferDesc.elementSize = 0;
    bufferDesc.size = size;
    bufferDesc.format = Format::Undefined;
    bufferDesc.usage = BufferUsage::UnorderedAccess;
    SLANG_RETURN_ON_FAIL(device->createBuffer(bufferDesc, nullptr, buffer.writeRef()));
    m_buffer = checked_cast<BufferImpl*>(buffer.get());

    D3D12_HEAP_PROPERTIES heapProps = makeHeapProperties(D3D12_HEAP_TYPE_READBACK);
    D3D12_RESOURCE_DESC resourceDesc = {};
    initBufferDesc(size, resourceDesc);
    SLANG_RETURN_ON_FAIL(m_readBackBuffer.initCommitted(
        device->m_device,
        heapProps,
        D3D12_HEAP_FLAG_NONE,
        resourceDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        device->m_allocator
    ));

    D3D12_RANGE readRange = {0, size};
    SLANG_RETURN_ON_FAIL(
        m_readBackBuffer.getResource()->Map(0, &readRange, reinterpret_cast<void**>(&m_mappedReadBackData))
    );

    m_queryType = m_desc.type;
    m_device = device;
    m_stride = stride;
    m_count = (uint32_t)m_desc.count;
    return SLANG_OK;
}

Result PlainBufferProxyQueryPoolImpl::isResultReady(uint32_t queryIndex, uint32_t count, bool* outReady)
{
    if (!outReady || !isValidQueryRange(queryIndex, count))
    {
        return SLANG_E_INVALID_ARG;
    }

    *outReady = false;
    QueryRangeInfo queryInfo = getQueryRangeInfo(queryIndex, count);
    if (queryInfo.state == QueryRangeState::Reset)
    {
        return SLANG_FAIL;
    }
    if (queryInfo.state == QueryRangeState::Resolved)
    {
        *outReady = true;
        return SLANG_OK;
    }

    CommandQueueImpl* queue = getDevice<DeviceImpl>()->m_queue.get();
    uint64_t submissionID = queryInfo.submissionID;
    if (queue->updateLastFinishedID() < submissionID)
    {
        return SLANG_OK;
    }

    markQueryRangeReady(queryIndex, count, queryInfo.submissionID);
    *outReady = true;

    return SLANG_OK;
}

Result PlainBufferProxyQueryPoolImpl::getResult(uint32_t queryIndex, uint32_t count, uint64_t* outData)
{
    if (!outData || !isValidQueryRange(queryIndex, count))
    {
        return SLANG_E_INVALID_ARG;
    }

    QueryRangeInfo queryInfo = getQueryRangeInfo(queryIndex, count);
    if (queryInfo.state == QueryRangeState::Reset)
    {
        return SLANG_FAIL;
    }
    if (count == 0)
    {
        return SLANG_OK;
    }

    DeviceImpl* device = getDevice<DeviceImpl>();
    CommandQueueImpl* queue = device->m_queue.get();
    uint64_t submissionID = queryInfo.submissionID;
    if (queue->updateLastFinishedID() < submissionID)
    {
        ResetEvent(queue->m_globalWaitHandle);
        SLANG_RETURN_ON_FAIL(queue->m_trackingFence->SetEventOnCompletion(submissionID, queue->m_globalWaitHandle));
        WaitForSingleObject(queue->m_globalWaitHandle, INFINITE);
        queue->updateLastFinishedID();
        queue->retireCommandBuffers();
    }

    memcpy(outData, m_mappedReadBackData + uint64_t(m_stride) * queryIndex, uint64_t(m_stride) * count);
    markQueryRangeReady(queryIndex, count, queryInfo.submissionID);

    return SLANG_OK;
}

} // namespace rhi::d3d12
