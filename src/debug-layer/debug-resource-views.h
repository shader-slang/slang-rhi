#pragma once

#include "debug-base.h"

namespace rhi::debug {

class DebugResourceView : public DebugObject<IResourceView>
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL;

public:
    IResourceView* getInterface(const Guid& guid);
    virtual SLANG_NO_THROW Desc* SLANG_MCALL getViewDesc() override;
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
    virtual SLANG_NO_THROW Desc* SLANG_MCALL getViewDesc() override;
};

} // namespace rhi::debug
