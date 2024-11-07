#pragma once

#include "cpu-base.h"

namespace rhi::cpu {

class BufferImpl : public Buffer
{
public:
    uint8_t* m_data = nullptr;

    BufferImpl(const BufferDesc& desc)
        : Buffer(desc)
    {
    }

    ~BufferImpl();

    virtual SLANG_NO_THROW DeviceAddress SLANG_MCALL getDeviceAddress() override;
};

} // namespace rhi::cpu
