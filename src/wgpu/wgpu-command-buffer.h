#pragma once

#include "wgpu-base.h"
#include "wgpu-shader-object.h"
#include "wgpu-command-encoder.h"

namespace rhi::wgpu {

class TransientResourceHeapImpl;

class CommandBufferImpl : public ICommandBuffer, public ComObject
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL
    ICommandBuffer* getInterface(const Guid& guid);

public:
    RefPtr<DeviceImpl> m_device;
    WGPUCommandBuffer m_commandBuffer = nullptr;

    ~CommandBufferImpl();

    // ICommandBuffer implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

} // namespace rhi::wgpu
