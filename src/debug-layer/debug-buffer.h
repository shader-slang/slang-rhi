#pragma once

#include "debug-base.h"

namespace rhi::debug {

class DebugBuffer : public DebugObject<IBuffer>
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL;

public:
    IBuffer* getInterface(const Guid& guid);
    virtual SLANG_NO_THROW BufferDesc* SLANG_MCALL getDesc() override;
    virtual SLANG_NO_THROW DeviceAddress SLANG_MCALL getDeviceAddress() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getSharedHandle(NativeHandle* outHandle) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL map(MemoryRange* rangeToRead, void** outPointer) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL unmap(MemoryRange* writtenRange) override;
};

} // namespace rhi::debug
