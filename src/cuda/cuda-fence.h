#pragma once

#include "cuda-base.h"

#include <mutex>

namespace rhi::cuda {

class FenceImpl : public Fence
{
public:
    RefPtr<DeviceImpl> m_device;
    uint64_t m_currentValue;
    std::mutex m_mutex;

    ~FenceImpl();

    // IFence implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getCurrentValue(uint64_t* outValue) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL setCurrentValue(uint64_t value) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getSharedHandle(NativeHandle* outHandle) override;
};

} // namespace rhi::cuda
