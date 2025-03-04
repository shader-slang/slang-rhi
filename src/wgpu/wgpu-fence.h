#pragma once

#include "wgpu-base.h"

#include <mutex>

namespace rhi::wgpu {

class FenceImpl : public Fence
{
public:
    uint64_t m_currentValue;
    std::mutex m_mutex;

    FenceImpl(Device* device, const FenceDesc& desc);
    ~FenceImpl();

    // IFence implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getCurrentValue(uint64_t* outValue) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL setCurrentValue(uint64_t value) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getSharedHandle(NativeHandle* outHandle) override;
};

} // namespace rhi::wgpu
