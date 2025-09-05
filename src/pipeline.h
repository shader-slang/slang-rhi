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
    virtual Pipeline* getConcretePipeline() const { return nullptr; }
    virtual void setConcretePipeline(Pipeline* pipeline) {}
};

class RenderPipeline : public IRenderPipeline, public Pipeline
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL
    IPipeline* getInterface(const Guid& guid);

public:
    RenderPipelineDesc m_desc;
    StructHolder m_descHolder;
    RefPtr<InputLayout> m_inputLayout;

    RenderPipeline(Device* device, const RenderPipelineDesc& desc);

    virtual PipelineType getType() const override { return PipelineType::Render; }

    // IPipeline interface
    virtual SLANG_NO_THROW const RenderPipelineDesc& SLANG_MCALL getDesc() override { return m_desc; }
    virtual SLANG_NO_THROW IShaderProgram* SLANG_MCALL getProgram() override { return m_program.get(); }
};

class VirtualRenderPipeline : public RenderPipeline
{
public:
    RefPtr<Pipeline> m_concretePipeline;

    VirtualRenderPipeline(Device* device, const RenderPipelineDesc& desc);

    virtual bool isVirtual() const override { return true; }
    virtual Pipeline* getConcretePipeline() const override { return m_concretePipeline.get(); }
    virtual void setConcretePipeline(Pipeline* pipeline) override { m_concretePipeline = pipeline; }

    // IRenderPipeline interface
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

class ComputePipeline : public IComputePipeline, public Pipeline
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL
    IPipeline* getInterface(const Guid& guid);

public:
    ComputePipelineDesc m_desc;
    StructHolder m_descHolder;

    ComputePipeline(Device* device, const ComputePipelineDesc& desc);

    virtual PipelineType getType() const override { return PipelineType::Compute; }

    // IPipeline interface
    virtual SLANG_NO_THROW const ComputePipelineDesc& SLANG_MCALL getDesc() override { return m_desc; }
    virtual SLANG_NO_THROW IShaderProgram* SLANG_MCALL getProgram() override { return m_program.get(); }
};

class VirtualComputePipeline : public ComputePipeline
{
public:
    RefPtr<Pipeline> m_concretePipeline;

    VirtualComputePipeline(Device* device, const ComputePipelineDesc& desc);

    virtual bool isVirtual() const override { return true; }
    virtual Pipeline* getConcretePipeline() const override { return m_concretePipeline.get(); }
    virtual void setConcretePipeline(Pipeline* pipeline) override { m_concretePipeline = pipeline; }

    // IComputePipeline interface
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

class RayTracingPipeline : public IRayTracingPipeline, public Pipeline
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL
    IPipeline* getInterface(const Guid& guid);

public:
    RayTracingPipelineDesc m_desc;
    StructHolder m_descHolder;

    RayTracingPipeline(Device* device, const RayTracingPipelineDesc& desc);

    virtual PipelineType getType() const override { return PipelineType::RayTracing; }

    // IPipeline interface
    virtual SLANG_NO_THROW const RayTracingPipelineDesc& SLANG_MCALL getDesc() override { return m_desc; }
    virtual SLANG_NO_THROW IShaderProgram* SLANG_MCALL getProgram() override { return m_program.get(); }
};

class VirtualRayTracingPipeline : public RayTracingPipeline
{
public:
    RefPtr<Pipeline> m_concretePipeline;

    VirtualRayTracingPipeline(Device* device, const RayTracingPipelineDesc& desc);

    virtual bool isVirtual() const override { return true; }
    virtual Pipeline* getConcretePipeline() const override { return m_concretePipeline.get(); }
    virtual void setConcretePipeline(Pipeline* pipeline) override { m_concretePipeline = pipeline; }

    // IRayTracingPipeline interface
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

} // namespace rhi
