#pragma once

#include "debug-base.h"

namespace rhi::debug {


class DebugTextureView : public DebugObject<ITextureView>
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL;

public:
    ITextureView* getInterface(const Guid& guid);
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

class DebugAccelerationStructure : public DebugObject<IAccelerationStructure>
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL;

public:
    IAccelerationStructure* getInterface(const Guid& guid);
    virtual SLANG_NO_THROW DeviceAddress SLANG_MCALL getDeviceAddress() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

} // namespace rhi::debug
