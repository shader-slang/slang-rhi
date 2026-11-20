#include "metal-query.h"
#include "metal-device.h"
#include "metal-command.h"
#include "metal-utils.h"

namespace rhi::metal {

QueryPoolImpl::QueryPoolImpl(Device* device, const QueryPoolDesc& desc)
    : QueryPool(device, desc)
{
}

QueryPoolImpl::~QueryPoolImpl() {}

Result QueryPoolImpl::init()
{
    DeviceImpl* device = getDevice<DeviceImpl>();

    MTL::CounterSet* counterSet = m_desc.type == QueryType::Timestamp ? device->m_timestampCounterSet.get() : nullptr;
    if (!counterSet)
    {
        return SLANG_E_NOT_AVAILABLE;
    }

    NS::SharedPtr<MTL::CounterSampleBufferDescriptor> counterSampleBufferDesc =
        NS::TransferPtr(MTL::CounterSampleBufferDescriptor::alloc()->init());
    counterSampleBufferDesc->setStorageMode(MTL::StorageModeShared);
    counterSampleBufferDesc->setSampleCount(m_desc.count);
    counterSampleBufferDesc->setCounterSet(counterSet);
    if (m_desc.label)
    {
        counterSampleBufferDesc->setLabel(createString(m_desc.label).get());
    }

    NS::Error* error;
    m_counterSampleBuffer =
        NS::TransferPtr(device->m_device->newCounterSampleBuffer(counterSampleBufferDesc.get(), &error));

    return m_counterSampleBuffer ? SLANG_OK : SLANG_FAIL;
}

Result QueryPoolImpl::getResultState(uint32_t queryIndex, uint32_t count, QueryResultState* outState)
{
    if (!outState || !isValidQueryRange(queryIndex, count))
    {
        return SLANG_E_INVALID_ARG;
    }
    if (count == 0)
    {
        *outState = QueryResultState::Resolved;
        return SLANG_OK;
    }

    QueryRangeInfo queryInfo = getQueryRangeInfo(queryIndex, count);
    if (queryInfo.state != QueryResultState::Pending)
    {
        *outState = queryInfo.state;
        return SLANG_OK;
    }

    CommandQueueImpl* queue = getDevice<DeviceImpl>()->m_queue.get();
    if (queue->updateLastFinishedID() < queryInfo.submissionID)
    {
        *outState = QueryResultState::Pending;
        return SLANG_OK;
    }

    markQueryRangeResolved(queryIndex, count, queryInfo.submissionID);
    *outState = QueryResultState::Resolved;
    return SLANG_OK;
}

Result QueryPoolImpl::getResult(uint32_t queryIndex, uint32_t count, uint64_t* outData)
{
    AUTORELEASEPOOL

    if (!outData || !isValidQueryRange(queryIndex, count))
    {
        return SLANG_E_INVALID_ARG;
    }
    if (count == 0)
    {
        return SLANG_OK;
    }

    QueryRangeInfo queryInfo = getQueryRangeInfo(queryIndex, count);
    if (queryInfo.state == QueryResultState::Reset)
    {
        return SLANG_FAIL;
    }

    CommandQueueImpl* queue = getDevice<DeviceImpl>()->m_queue.get();
    if (queryInfo.state == QueryResultState::Pending && queue->updateLastFinishedID() < queryInfo.submissionID)
    {
        SLANG_RETURN_ON_FAIL(queue->waitOnHost());
    }

    NS::Data* data = m_counterSampleBuffer->resolveCounterRange(NS::Range(queryIndex, count));
    if (!data || data->length() < count * sizeof(MTL::CounterResultTimestamp))
    {
        return SLANG_FAIL;
    }
    const MTL::CounterResultTimestamp* samples = static_cast<const MTL::CounterResultTimestamp*>(data->mutableBytes());
    for (uint32_t i = 0; i < count; ++i)
    {
        outData[i] = samples[i].timestamp;
    }

    markQueryRangeResolved(queryIndex, count, queryInfo.submissionID);
    return SLANG_OK;
}

} // namespace rhi::metal
