#include "cuda-fence.h"
#include "cuda-device.h"
#include "cuda-command.h"

#include <thread>
#include <chrono>

namespace rhi::cuda {

FenceImpl::FenceImpl(Device* device, const FenceDesc& desc)
    : Fence(device, desc)
    , m_lastSignalledValue(desc.initialValue)
{
}

FenceImpl::~FenceImpl()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // Destroy all pending events.
    for (const auto& pendingEvent : m_pendingEvents)
    {
        if (pendingEvent.event)
        {
            SLANG_CUDA_ASSERT_ON_FAIL(cuEventDestroy(pendingEvent.event));
        }
    }
    m_pendingEvents.clear();
}

Result FenceImpl::getCurrentValue(uint64_t* outValue)
{
    // Host-side get value simply polls pending events and returns
    // the current value.
    std::lock_guard<std::mutex> lock(m_mutex);
    SLANG_RETURN_ON_FAIL(flush());
    *outValue = m_currentValue;
    return SLANG_OK;
}

Result FenceImpl::setCurrentValue(uint64_t value)
{
    // Host-side set value polls pending events to get latest value,
    // then updates it if necessary.
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_lastSignalledValue >= value)
        return SLANG_E_INVALID_ARG;
    SLANG_RETURN_ON_FAIL(flush());

    // As we know this is the highest signal ever made, this means that:
    // - the current value is by definition lower, regardless of whether set from host or device
    // - this 'set' may mean existing pending events are now stale, regardless of whether already associated with a
    // stream

    // Set current value.
    SLANG_RHI_ASSERT(m_currentValue < value);
    m_currentValue = value;
    m_lastSignalledValue = value;

    // Flush any pending events that are now older than the current value.
    SLANG_RETURN_ON_FAIL(triggerStaleEvents());

    debugCheckPendingEvents();
    return SLANG_OK;
}

Result FenceImpl::flush()
{
    debugCheckPendingEvents();

    // Iterate all pending events and check their status.
    uint64_t origValue = m_currentValue;
    for (auto it = m_pendingEvents.begin(); it != m_pendingEvents.end();)
    {
        PendingEvent& pendingEvent = *it;
        CUresult result = cuEventQuery(pendingEvent.event);
        if (result == CUDA_SUCCESS)
        {
            // Event is signaled, update the current value.
            m_currentValue = std::max(pendingEvent.value, m_currentValue);
            SLANG_CUDA_RETURN_ON_FAIL(cuEventDestroy(pendingEvent.event));
            it = m_pendingEvents.erase(it);
        }
        else if (result == CUDA_ERROR_NOT_READY)
        {
            // Event is not ready yet, continue to the next one.
            ++it;
        }
        else
        {
            // An error occurred, return CUDA error.
            SLANG_CUDA_RETURN_ON_FAIL(result);
            break;
        }
    }

    // If current value changed,
    if (origValue != m_currentValue)
        triggerStaleEvents();

    debugCheckPendingEvents();

    return SLANG_OK;
}

Result FenceImpl::triggerStaleEvents()
{
    // Iterate all pending events and check for any that are out of date
    // (their value is <= m_currentValue).
    for (auto it = m_pendingEvents.begin(); it != m_pendingEvents.end();)
    {
        PendingEvent& pendingEvent = *it;
        if (pendingEvent.value <= m_currentValue)
        {
            SLANG_CUDA_RETURN_ON_FAIL(cuEventDestroy(pendingEvent.event));
            it = m_pendingEvents.erase(it);
        }
        else
        {
            ++it;
        }
    }
    return SLANG_OK;
}

Result FenceImpl::signalFromStream(uint64_t value, CUstream stream)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_lastSignalledValue >= value)
        return SLANG_E_INVALID_ARG;
    SLANG_RETURN_ON_FAIL(flush());

    // Try to find the event that corresponds to the value.
    // If we don't find it, but we find a pending event with a higher value,
    // we create a new event for the value and insert it into the pending events list.
    CUevent event = nullptr;
    for (auto pendingEventIt = m_pendingEvents.begin(); pendingEventIt != m_pendingEvents.end(); ++pendingEventIt)
    {
        PendingEvent& pendingEvent = *pendingEventIt;
        if (pendingEvent.value == value)
        {
            // Got event, mark it as added to stream.
            event = pendingEvent.event;
            pendingEvent.isAddedToStream = true;
            break;
        }
        if (pendingEvent.value > value)
        {
            // Found event with higher value - insert this one.
            SLANG_CUDA_RETURN_ON_FAIL(cuEventCreate(&event, 0));
            m_pendingEvents.insert(pendingEventIt, {event, value, true});
            break;
        }
    }

    // Nothing found, create a new event and stick it on the end.
    if (!event)
    {
        SLANG_CUDA_RETURN_ON_FAIL(cuEventCreate(&event, 0));
        m_pendingEvents.push_back({event, value, true});
    }

    // Record the event in the stream.
    SLANG_CUDA_RETURN_ON_FAIL(cuEventRecord(event, stream));
    m_lastSignalledValue = value;

    debugCheckPendingEvents();
    return SLANG_OK;
}

Result FenceImpl::waitOnStream(uint64_t value, CUstream stream)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    SLANG_RETURN_ON_FAIL(flush());

    // Nothing to do if the host already knows the value is signaled.
    if (value <= m_currentValue)
    {
        return SLANG_OK;
    }

    // Try to find the event that corresponds to the value.
    // If we don't find it, but we find a pending event with a higher value,
    // we create a new event for the value and insert it into the pending events list.
    CUevent event = nullptr;
    for (auto pendingEventIt = m_pendingEvents.begin(); pendingEventIt != m_pendingEvents.end(); ++pendingEventIt)
    {
        PendingEvent& pendingEvent = *pendingEventIt;
        if (pendingEvent.value == value)
        {
            // Got event.
            event = pendingEvent.event;
            break;
        }
        // if (pendingEvent.value > value)
        //{
        //     // Found event with higher value - insert this one.
        //     SLANG_CUDA_RETURN_ON_FAIL(cuEventCreate(&event, 0));
        //     m_pendingEvents.insert(pendingEventIt, {event, value, false});
        //     break;
        // }
    }

    // Nothing found, create a new event and stick it on the end.
    // if (!event)
    //{
    //    SLANG_CUDA_RETURN_ON_FAIL(cuEventCreate(&event, 0));
    //    m_pendingEvents.push_back({event, value, false});
    //}
    if (!event)
    {
        return SLANG_E_NOT_AVAILABLE;
    }

    // Wait on the event from the stream.
    SLANG_CUDA_RETURN_ON_FAIL(cuStreamWaitEvent(stream, event, 0));

    debugCheckPendingEvents();
    return SLANG_OK;
}

void FenceImpl::debugCheckPendingEvents()
{
    // This does a series of checks of both the current value and pending events
    // lists to make sure the fence is in a valid state.

    // Pending events queue should be in order and have no duplicates.
    for (size_t i = 1; i < m_pendingEvents.size(); ++i)
    {
        SLANG_RHI_ASSERT(m_pendingEvents[i - 1].value < m_pendingEvents[i].value);
    }

    // Current value should be <= the last signaled value
    SLANG_RHI_ASSERT(m_currentValue <= m_lastSignalledValue);

    // All pending events associated with a stream should be <= the last signaled value
    // and any not associated with a stream should be > the current value.
    for (const auto& pendingEvent : m_pendingEvents)
    {
        if (pendingEvent.isAddedToStream)
        {
            SLANG_RHI_ASSERT(pendingEvent.value <= m_lastSignalledValue);
        }
        else
        {
            SLANG_RHI_ASSERT(pendingEvent.value > m_currentValue);
        }
    }

    // All pending events should be > the current value (as we clear stale events)
    for (const auto& pendingEvent : m_pendingEvents)
    {
        SLANG_RHI_ASSERT(pendingEvent.value > m_currentValue);
    }

    // Pending events should consist of M that are associated with a stream,
    // followed by N that are not. i.e. we should never discover an associated
    // one after an unassociated one.
    size_t firstUnassociated = m_pendingEvents.size();
    for (size_t i = 0; i < m_pendingEvents.size(); ++i)
    {
        if (!m_pendingEvents[i].isAddedToStream)
        {
            firstUnassociated = i;
            break;
        }
    }
    for (size_t i = 0; i < m_pendingEvents.size(); ++i)
    {
        if (i >= firstUnassociated)
        {
            // All events after the first unassociated one should not be associated with a stream.
            SLANG_RHI_ASSERT(!m_pendingEvents[i].isAddedToStream);
        }
        else
        {
            // All events before the first unassociated one should be associated with a stream.
            SLANG_RHI_ASSERT(m_pendingEvents[i].isAddedToStream);
        }
    }
}


Result FenceImpl::getNativeHandle(NativeHandle* outHandle)
{
    return SLANG_E_NOT_AVAILABLE;
}

Result FenceImpl::getSharedHandle(NativeHandle* outHandle)
{
    return SLANG_E_NOT_AVAILABLE;
}

Result DeviceImpl::createFence(const FenceDesc& desc, IFence** outFence)
{
    SLANG_CUDA_CTX_SCOPE(this);

    RefPtr<FenceImpl> fence = new FenceImpl(this, desc);
    fence->m_currentValue = desc.initialValue;
    returnComPtr(outFence, fence);
    return SLANG_OK;
}


Result DeviceImpl::waitForFences(
    uint32_t fenceCount,
    IFence** fences,
    const uint64_t* fenceValues,
    bool waitForAll,
    uint64_t timeout
)
{
    // TODO: In theory we could have a fast-path for when timeout was 'infinite' and 'waitForAll' is true (or just 1
    // fence) in which we just call cuEventSynchronize. However as soon as we want to support waiting for just one, or
    // having a timeout it has to be busy loop as below.

    // List of fences we still wait on.
    short_vector<FenceImpl*> waitFences;
    waitFences.resize(fenceCount);
    for (uint32_t i = 0; i < fenceCount; ++i)
    {
        waitFences[i] = checked_cast<FenceImpl*>(fences[i]);
    }

    // Wait for all fences to be signaled.
    size_t waitCount = waitFences.size();
    auto startTime = std::chrono::high_resolution_clock::now();
    auto endTime = startTime + std::chrono::nanoseconds(timeout);
    auto currentTime = startTime;
    while (currentTime <= endTime || timeout == kTimeoutInfinite)
    {
        for (uint32_t i = 0; i < fenceCount && waitFences[i]; ++i)
        {
            FenceImpl* fence = waitFences[i];
            uint64_t value;
            SLANG_RETURN_ON_FAIL(fence->getCurrentValue(&value));
            if (value >= fenceValues[i])
            {
                waitFences[i] = nullptr;
                waitCount--;
            }
        }

        // Return immediately if wait condition is already met.
        if (waitCount == 0 || (!waitForAll && waitCount < fenceCount))
        {
            return SLANG_OK;
        }

        std::this_thread::yield();
        currentTime = std::chrono::high_resolution_clock::now();
    }

    return SLANG_E_TIME_OUT;
}

} // namespace rhi::cuda
