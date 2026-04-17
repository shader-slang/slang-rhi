#pragma once

#include "metal-base.h"

namespace rhi::metal {

class BufferImpl : public Buffer
{
public:
    BufferImpl(Device* device, const BufferDesc& desc);
    ~BufferImpl();

    virtual void deleteThis() override;

    // IResource implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;

    // IBuffer implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getSharedHandle(NativeHandle* outHandle) override;
    virtual SLANG_NO_THROW DeviceAddress SLANG_MCALL getDeviceAddress() override;

public:
    NS::SharedPtr<MTL::Buffer> m_buffer;
    CpuAccessMode m_lastCpuAccessMode;
};

} // namespace rhi::metal
