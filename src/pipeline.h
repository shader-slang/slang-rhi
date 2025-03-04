#pragma once

#include <slang-rhi.h>

#include "core/common.h"

#include "shader.h"

#include "rhi-shared-fwd.h"
#include "device-child.h"

namespace rhi {

enum class PipelineType
{
    Render,
    Compute,
    RayTracing,
};

class Pipeline : public DeviceChild
{
public:
    RefPtr<ShaderProgram> m_program;

    Pipeline(Device* device)
        : DeviceChild(device)
    {
    }

    virtual PipelineType getType() const = 0;
    virtual bool isVirtual() const { return false; }
};

class RenderPipeline : public IRenderPipeline, public Pipeline
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL
    IPipeline* getInterface(const Guid& guid);

public:
    RenderPipeline(Device* device)
        : Pipeline(device)
    {
    }

    virtual PipelineType getType() const override { return PipelineType::Render; }

    // IPipeline interface
    virtual SLANG_NO_THROW IShaderProgram* SLANG_MCALL getProgram() override { return m_program.get(); }
};

class VirtualRenderPipeline : public RenderPipeline
{
public:
    RenderPipelineDesc m_desc;
    StructHolder m_descHolder;
    RefPtr<InputLayout> m_inputLayout;

    VirtualRenderPipeline(Device* device, const RenderPipelineDesc& desc);

    virtual bool isVirtual() const override { return true; }

    // IRenderPipeline interface
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

class ComputePipeline : public IComputePipeline, public Pipeline
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL
    IPipeline* getInterface(const Guid& guid);

public:
    ComputePipeline(Device* device)
        : Pipeline(device)
    {
    }

    virtual PipelineType getType() const override { return PipelineType::Compute; }

    // IPipeline interface
    virtual SLANG_NO_THROW IShaderProgram* SLANG_MCALL getProgram() override { return m_program.get(); }
};

class VirtualComputePipeline : public ComputePipeline
{
public:
    ComputePipelineDesc m_desc;

    VirtualComputePipeline(Device* device, const ComputePipelineDesc& desc);

    virtual bool isVirtual() const override { return true; }

    // IComputePipeline interface
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

class RayTracingPipeline : public IRayTracingPipeline, public Pipeline
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL
    IPipeline* getInterface(const Guid& guid);

public:
    RayTracingPipeline(Device* device)
        : Pipeline(device)
    {
    }

    virtual PipelineType getType() const override { return PipelineType::RayTracing; }

    // IPipeline interface
    virtual SLANG_NO_THROW IShaderProgram* SLANG_MCALL getProgram() override { return m_program.get(); }
};

class VirtualRayTracingPipeline : public RayTracingPipeline
{
public:
    RayTracingPipelineDesc m_desc;
    StructHolder m_descHolder;

    VirtualRayTracingPipeline(Device* device, const RayTracingPipelineDesc& desc);

    virtual bool isVirtual() const override { return true; }

    // IRayTracingPipeline interface
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

} // namespace rhi
