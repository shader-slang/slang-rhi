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
    /// Set when a submitted copy or clear writes this upload buffer. Mapping then refreshes the
    /// CPU copy from the GPU buffer first.
    std::atomic<bool> m_uploadDataStale{false};
    /// Imported or exported native buffers can be modified without going through this wrapper.
    std::atomic<bool> m_uploadDataExternallyMutable{false};
};

} // namespace rhi::wgpu
