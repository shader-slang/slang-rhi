#pragma once

#include "debug-base.h"

namespace rhi::debug {

class DebugBuffer : public DebugObject<IBuffer>
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL;

    SLANG_RHI_DEBUG_OBJECT_CONSTRUCTOR(DebugBuffer);

public:
    IBuffer* getInterface(const Guid& guid);
    virtual SLANG_NO_THROW const BufferDesc& SLANG_MCALL getDesc() override;
    virtual SLANG_NO_THROW DeviceAddress SLANG_MCALL getDeviceAddress() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getSharedHandle(NativeHandle* outHandle) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL map(BufferRange* rangeToRead, void** outPointer) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL unmap(BufferRange* writtenRange) override;
};

} // namespace rhi::debug
