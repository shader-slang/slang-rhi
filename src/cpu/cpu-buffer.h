#pragma once

#include "cpu-base.h"

namespace rhi::cpu {

class BufferImpl : public Buffer
{
public:
    BufferImpl(const BufferDesc& desc)
        : Buffer(desc)
    {
    }

    ~BufferImpl();

    Result init();

    Result setData(size_t offset, size_t size, void const* data);

    void* m_data = nullptr;

    virtual SLANG_NO_THROW DeviceAddress SLANG_MCALL getDeviceAddress() override;

    virtual SLANG_NO_THROW Result SLANG_MCALL map(BufferRange* rangeToRead, void** outPointer) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL unmap(BufferRange* writtenRange) override;
};

} // namespace rhi::cpu
