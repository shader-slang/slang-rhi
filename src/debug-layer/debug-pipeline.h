#pragma once

#include "debug-base.h"

namespace rhi::debug {

class DebugRenderPipeline : public DebugObject<IRenderPipeline>
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL;

public:
    IRenderPipeline* getInterface(const Guid& guid);
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

class DebugComputePipeline : public DebugObject<IComputePipeline>
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL;

public:
    IComputePipeline* getInterface(const Guid& guid);
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

class DebugRayTracingPipeline : public DebugObject<IRayTracingPipeline>
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL;

public:
    IRayTracingPipeline* getInterface(const Guid& guid);
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

} // namespace rhi::debug
