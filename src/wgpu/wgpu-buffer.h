#pragma once

#include "wgpu-base.h"

#include <atomic>
#include <memory>

namespace rhi::wgpu {

class BufferImpl : public Buffer
{
public:
    BufferImpl(Device* device, const BufferDesc& desc);
    ~BufferImpl();

    // IResource implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;

    // IBuffer implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getSharedHandle(NativeHandle* outHandle) override;
    virtual SLANG_NO_THROW DeviceAddress SLANG_MCALL getDeviceAddress() override;

public:
    WGPUBuffer m_buffer = nullptr;

    /// CPU copy of a MemoryType::Upload buffer.
    /// WebGPU only allows mapping for writing together with CopySrc usage, and mapping is
    /// asynchronous. Upload buffers are therefore regular GPU buffers with a CPU copy that is
    /// returned by mapBuffer() and written to the GPU buffer by unmapBuffer().
    std::unique_ptr<uint8_t[]> m_uploadData;
    /// Sticky flag for buffers writable by shaders, recorded copies/clears, or native code.
    /// Once set, every mapping refreshes the CPU copy. Never cleared: a recorded write may be
    /// submitted after a mapping, and native writes cannot be tracked by this wrapper.
    std::atomic<bool> m_uploadDataNeedsReadback{false};
};

} // namespace rhi::wgpu
