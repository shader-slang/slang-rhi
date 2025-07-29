#include "metal-query.h"
#include "metal-device.h"
#include "metal-utils.h"

namespace rhi::metal {

QueryPoolImpl::QueryPoolImpl(Device* device, const QueryPoolDesc& desc)
    : QueryPool(device, desc)
{
}

QueryPoolImpl::~QueryPoolImpl() {}

static MTL::CounterSet* findCounterSet(MTL::Device* device, QueryType queryType)
{
    if (queryType != QueryType::Timestamp)
    {
        return nullptr;
    }

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

Result QueryPoolImpl::init()
{
    DeviceImpl* device = getDevice<DeviceImpl>();

    MTL::CounterSet* counterSet = findCounterSet(device->m_device.get(), m_desc.type);
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

Result QueryPoolImpl::getResult(uint32_t queryIndex, uint32_t count, uint64_t* data)
{
    if (count == 0)
    {
        return SLANG_OK;
    }

    NS::Data* rawData = m_counterSampleBuffer->resolveCounterRange(NS::Range(queryIndex, count));
    static_assert(sizeof(MTL::CounterResultTimestamp) == sizeof(uint64_t));
    std::memcpy(data, rawData, count * sizeof(uint64_t));

    return SLANG_OK;
}

} // namespace rhi::metal
