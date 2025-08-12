#pragma once

#include "webgpu-base.h"

namespace rhi::webgpu {

class BufferImpl : public Buffer
{
public:
    WebGPUBuffer m_buffer = nullptr;

    BufferImpl(Device* device, const BufferDesc& desc);
    ~BufferImpl();

    // IBuffer implementation
    virtual SLANG_NO_THROW DeviceAddress SLANG_MCALL getDeviceAddress() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getSharedHandle(NativeHandle* outHandle) override;
};

} // namespace rhi::webgpu
