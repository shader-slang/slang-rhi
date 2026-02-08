#pragma once

#include "cpu-base.h"

namespace rhi::cpu {

class BufferImpl : public Buffer
{
public:
    BufferImpl(Device* device, const BufferDesc& desc);
    ~BufferImpl();

    // IBuffer implementation
    virtual SLANG_NO_THROW DeviceAddress SLANG_MCALL getDeviceAddress() override;

public:
    uint8_t* m_data = nullptr;
};

} // namespace rhi::cpu
