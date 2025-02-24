#pragma once

#include "metal-base.h"

namespace rhi::metal {

class FenceImpl : public Fence
{
public:
    RefPtr<DeviceImpl> m_device;
    NS::SharedPtr<MTL::SharedEvent> m_event;
    NS::SharedPtr<MTL::SharedEventListener> m_eventListener;

    ~FenceImpl();

    Result init(DeviceImpl* device, const FenceDesc& desc);

    bool waitForFence(uint64_t value, uint64_t timeout);

    virtual SLANG_NO_THROW Result SLANG_MCALL getCurrentValue(uint64_t* outValue) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL setCurrentValue(uint64_t value) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getSharedHandle(NativeHandle* outHandle) override;
};

} // namespace rhi::metal
