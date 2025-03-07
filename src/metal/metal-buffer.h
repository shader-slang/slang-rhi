#pragma once

#include "metal-base.h"

namespace rhi::metal {

class BufferImpl : public Buffer
{
public:
    NS::SharedPtr<MTL::Buffer> m_buffer;
    CpuAccessMode m_lastCpuAccessMode;

    BufferImpl(Device* device, const BufferDesc& desc);
    ~BufferImpl();

    virtual SLANG_NO_THROW DeviceAddress SLANG_MCALL getDeviceAddress() override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getSharedHandle(NativeHandle* outHandle) override;
};

} // namespace rhi::metal
