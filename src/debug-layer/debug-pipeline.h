#pragma once

#include "debug-base.h"

namespace rhi::debug {

class DebugPipeline : public DebugObject<IPipeline>
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL;
    IPipeline* getInterface(const Guid& guid);

    SLANG_RHI_DEBUG_OBJECT_CONSTRUCTOR(DebugPipeline);
};

class DebugRenderPipeline : public DebugObject<IRenderPipeline>
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL;
    IRenderPipeline* getInterface(const Guid& guid);

    SLANG_RHI_DEBUG_OBJECT_CONSTRUCTOR(DebugRenderPipeline);

public:
    // IRenderPipeline implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

class DebugComputePipeline : public DebugObject<IComputePipeline>
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL;
    IComputePipeline* getInterface(const Guid& guid);

    SLANG_RHI_DEBUG_OBJECT_CONSTRUCTOR(DebugComputePipeline);

public:
    // IComputePipeline implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

class DebugRayTracingPipeline : public DebugObject<IRayTracingPipeline>
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL;
    IRayTracingPipeline* getInterface(const Guid& guid);

    SLANG_RHI_DEBUG_OBJECT_CONSTRUCTOR(DebugRayTracingPipeline);

public:
    // IRayTracingPipeline implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

class DebugWorkGraphPipeline : public DebugObject<IWorkGraphPipeline>
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL;
    IWorkGraphPipeline* getInterface(const Guid& guid);

    SLANG_RHI_DEBUG_OBJECT_CONSTRUCTOR(DebugWorkGraphPipeline);

public:
    // IWorkGraphPipeline implementation
    virtual SLANG_NO_THROW const WorkGraphPipelineDesc& SLANG_MCALL getDesc() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getWorkGraphMemoryRequirements(
        WorkGraphMemoryRequirements* outRequirements
    ) override;
};

} // namespace rhi::debug
