#include "wgpu-fence.h"
#include "wgpu-device.h"

#include <thread>
#include <chrono>

namespace rhi::wgpu {

FenceImpl::~FenceImpl() {}

Result FenceImpl::getCurrentValue(uint64_t* outValue)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    *outValue = m_currentValue;
    return SLANG_OK;
}

Result FenceImpl::setCurrentValue(uint64_t value)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_currentValue = value;
    return SLANG_OK;
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
    RefPtr<FenceImpl> fence = new FenceImpl();
    fence->m_device = this;
    fence->m_currentValue = desc.initialValue;
    returnComPtr(outFence, fence);
    return SLANG_OK;
}

Result DeviceImpl::waitForFences(
    uint32_t fenceCount,
    IFence** fences,
    uint64_t* fenceValues,
    bool waitForAll,
    uint64_t timeout
)
{
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

} // namespace rhi::wgpu
