#pragma once

#include "metal-base.h"
#include "metal-device.h"

namespace rhi::metal {

class BufferImpl : public Buffer
{
public:
    typedef Buffer Parent;

    BreakableReference<DeviceImpl> m_device;
    NS::SharedPtr<MTL::Buffer> m_buffer;

    BufferImpl(const BufferDesc& desc, DeviceImpl* device);
    ~BufferImpl();

    virtual SLANG_NO_THROW DeviceAddress SLANG_MCALL getDeviceAddress() override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeResourceHandle(InteropHandle* outHandle) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getSharedHandle(InteropHandle* outHandle) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL map(MemoryRange* rangeToRead, void** outPointer) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL unmap(MemoryRange* writtenRange) override;
};

} // namespace rhi::metal
