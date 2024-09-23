#pragma once

#include "metal-base.h"
#include "metal-device.h"

namespace rhi::metal {

class BufferImpl : public Buffer
{
public:
    NS::SharedPtr<MTL::Buffer> m_buffer;

    BufferImpl(const BufferDesc& desc);
    ~BufferImpl();

    virtual SLANG_NO_THROW DeviceAddress SLANG_MCALL getDeviceAddress() override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getSharedHandle(NativeHandle* outHandle) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL map(BufferRange* rangeToRead, void** outPointer) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL unmap(BufferRange* writtenRange) override;
};

} // namespace rhi::metal
