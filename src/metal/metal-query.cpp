#include "metal-query.h"
#include "metal-buffer.h"
#include "metal-command.h"
#include "metal-device.h"
#include "metal-utils.h"

namespace rhi::metal {

QueryPoolImpl::QueryPoolImpl(Device* device, const QueryPoolDesc& desc)
    : QueryPool(device, desc)
{
}

QueryPoolImpl::~QueryPoolImpl() {}

MTL::CounterSet* findTimestampCounterSet(MTL::Device* device)
{
    for (int i = 0; i < device->counterSets()->count(); ++i)
    {
        MTL::CounterSet* counterSet = static_cast<MTL::CounterSet*>(device->counterSets()->object(i));
        if (!counterSet->name()->isEqualToString(MTL::CommonCounterSetTimestamp))
        {
            continue;
        }
        for (int j = 0; j < counterSet->counters()->count(); ++j)
        {
            MTL::Counter* counter = static_cast<MTL::Counter*>(counterSet->counters()->object(j));
            if (counter->name()->isEqualToString(MTL::CommonCounterTimestamp))
            {
                return counterSet;
            }
        }
    }
    return nullptr;
}

bool supportsTimestampQueries(MTL::Device* device)
{
    // Stage-boundary-only devices cannot implement ICommandEncoder::writeTimestamp at
    // arbitrary command positions, so require every boundary used by the recorder.
    return findTimestampCounterSet(device) &&
           device->supportsCounterSampling(MTL::CounterSamplingPointAtDrawBoundary) &&
           device->supportsCounterSampling(MTL::CounterSamplingPointAtDispatchBoundary) &&
           device->supportsCounterSampling(MTL::CounterSamplingPointAtBlitBoundary);
}

Result QueryPoolImpl::init()
{
    DeviceImpl* device = getDevice<DeviceImpl>();

    if (m_desc.type != QueryType::Timestamp)
    {
        return SLANG_E_NOT_AVAILABLE;
    }

    MTL::CounterSet* counterSet = findTimestampCounterSet(device->m_device.get());
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
    if (!m_counterSampleBuffer)
    {
        return SLANG_FAIL;
    }

    m_readbackBuffer = NS::TransferPtr(device->m_device->newBuffer(
        sizeof(MTL::CounterResultTimestamp) * m_desc.count,
        MTL::ResourceStorageModeShared
    ));

    return m_readbackBuffer ? SLANG_OK : SLANG_FAIL;
}

Result QueryPoolImpl::getResultState(uint32_t queryIndex, uint32_t count, QueryResultState* outState)
{
    if (!outState || !isValidQueryRange(queryIndex, count))
    {
        return SLANG_E_INVALID_ARG;
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
    SLANG_RETURN_ON_FAIL(queue->waitForSubmission(queryInfo.submissionID));

    const MTL::CounterResultTimestamp* results =
        static_cast<const MTL::CounterResultTimestamp*>(m_readbackBuffer->contents());
    for (uint32_t i = 0; i < count; ++i)
    {
        if (results[queryIndex + i].timestamp == MTL::CounterErrorValue)
        {
            return SLANG_FAIL;
        }
        outData[i] = results[queryIndex + i].timestamp;
    }

    markQueryRangeResolved(queryIndex, count, queryInfo.submissionID);
    return SLANG_OK;
}

} // namespace rhi::metal
