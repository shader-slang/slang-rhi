#pragma once

#include "debug-base.h"

namespace rhi::debug {

class DebugPipeline : public DebugObject<IPipeline>
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL;

    SLANG_RHI_DEBUG_OBJECT_CONSTRUCTOR(DebugPipeline);

public:
    IPipeline* getInterface(const Guid& guid);
};

class DebugRenderPipeline : public DebugObject<IRenderPipeline>
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL;

    SLANG_RHI_DEBUG_OBJECT_CONSTRUCTOR(DebugRenderPipeline);

public:
    IRenderPipeline* getInterface(const Guid& guid);
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

class DebugComputePipeline : public DebugObject<IComputePipeline>
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL;

    SLANG_RHI_DEBUG_OBJECT_CONSTRUCTOR(DebugComputePipeline);

public:
    IComputePipeline* getInterface(const Guid& guid);
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

class DebugRayTracingPipeline : public DebugObject<IRayTracingPipeline>
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL;

    SLANG_RHI_DEBUG_OBJECT_CONSTRUCTOR(DebugRayTracingPipeline);

public:
    IRayTracingPipeline* getInterface(const Guid& guid);
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

} // namespace rhi::debug
