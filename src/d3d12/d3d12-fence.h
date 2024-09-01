#pragma once

#include "d3d12-base.h"

namespace rhi::d3d12 {

class FenceImpl : public FenceBase
{
public:
    ComPtr<ID3D12Fence> m_fence;
    HANDLE m_waitEvent = 0;

    ~FenceImpl();

    HANDLE getWaitEvent();

    Result init(DeviceImpl* device, const IFence::Desc& desc);

    virtual SLANG_NO_THROW Result SLANG_MCALL getCurrentValue(uint64_t* outValue) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL setCurrentValue(uint64_t value) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getSharedHandle(NativeHandle* outHandle) override;
};

} // namespace rhi::d3d12
