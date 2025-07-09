#pragma once

#include "cuda-base.h"

#include <mutex>

namespace rhi::cuda {

class FenceImpl : public Fence
{
public:
    struct PendingEvent
    {
        CUevent event;
        uint64_t value;
        bool isAddedToStream;
    };

    std::vector<PendingEvent> m_pendingEvents;

    uint64_t m_lastSignalledValue;
    uint64_t m_currentValue;
    std::mutex m_mutex;
    DeviceImpl* m_deviceImpl;

    FenceImpl(Device* device, const FenceDesc& desc);
    ~FenceImpl();

    // IFence implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getCurrentValue(uint64_t* outValue) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL setCurrentValue(uint64_t value) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getSharedHandle(NativeHandle* outHandle) override;

    Result signalFromStream(uint64_t value, CUstream stream);

    Result waitOnStream(uint64_t value, CUstream);

private:
    Result flush();
    Result triggerStaleEvents();

    void debugCheckPendingEvents();
};

} // namespace rhi::cuda
