#pragma once

#include "wgpu-base.h"

namespace rhi::wgpu {

class BufferImpl : public Buffer
{
public:
    DeviceImpl* m_device;
    WGPUBuffer m_buffer = nullptr;
    WGPUMapMode m_mapMode = WGPUMapMode_None;
    bool m_isMapped = false;

    BufferImpl(DeviceImpl* device, const BufferDesc& desc);
    ~BufferImpl();

    // IBuffer implementation
    virtual SLANG_NO_THROW DeviceAddress SLANG_MCALL getDeviceAddress() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getSharedHandle(NativeHandle* outHandle) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL map(MemoryRange* rangeToRead, void** outPointer) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL unmap(MemoryRange* writtenRange) override;
};

} // namespace rhi::wgpu
