#pragma once

#include "cpu-base.h"

namespace rhi::cpu {

class BufferImpl : public Buffer
{
public:
    BufferImpl(const Desc& _desc)
        : Buffer(_desc)
    {
    }

    ~BufferImpl();

    Result init();

    Result setData(size_t offset, size_t size, void const* data);

    void* m_data = nullptr;

    virtual SLANG_NO_THROW DeviceAddress SLANG_MCALL getDeviceAddress() override;

    virtual SLANG_NO_THROW Result SLANG_MCALL map(MemoryRange* rangeToRead, void** outPointer) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL unmap(MemoryRange* writtenRange) override;
};

} // namespace rhi::cpu
