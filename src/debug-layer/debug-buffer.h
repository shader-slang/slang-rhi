#pragma once

#include "debug-base.h"

namespace rhi::debug {

class DebugBuffer : public DebugObject<IBuffer>
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL;

public:
    IBuffer* getInterface(const Guid& guid);
    virtual SLANG_NO_THROW Type SLANG_MCALL getType() override;
    virtual SLANG_NO_THROW Desc* SLANG_MCALL getDesc() override;
    virtual SLANG_NO_THROW DeviceAddress SLANG_MCALL getDeviceAddress() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeResourceHandle(InteropHandle* outHandle) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getSharedHandle(InteropHandle* outHandle) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL setDebugName(const char* name) override;
    virtual SLANG_NO_THROW const char* SLANG_MCALL getDebugName() override;

    virtual SLANG_NO_THROW Result SLANG_MCALL map(MemoryRange* rangeToRead, void** outPointer) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL unmap(MemoryRange* writtenRange) override;
};

} // namespace rhi::debug
