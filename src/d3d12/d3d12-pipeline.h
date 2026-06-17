#pragma once

#include "d3d12-base.h"

namespace rhi::d3d12 {

class RenderPipelineImpl : public RenderPipeline
{
public:
    RefPtr<InputLayoutImpl> m_inputLayout;
    RefPtr<RootShaderObjectLayoutImpl> m_rootObjectLayout;
    ComPtr<ID3D12PipelineState> m_pipelineState;
    D3D_PRIMITIVE_TOPOLOGY m_primitiveTopology;

    RenderPipelineImpl(Device* device, const RenderPipelineDesc& desc);

    // IRenderPipeline implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

class ComputePipelineImpl : public ComputePipeline
{
public:
    RefPtr<RootShaderObjectLayoutImpl> m_rootObjectLayout;
    ComPtr<ID3D12PipelineState> m_pipelineState;

    ComputePipelineImpl(Device* device, const ComputePipelineDesc& desc);

    // IComputePipeline implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

class RayTracingPipelineImpl : public RayTracingPipeline
{
public:
    RefPtr<RootShaderObjectLayoutImpl> m_rootObjectLayout;
    ComPtr<ID3D12StateObject> m_stateObject;
    std::map<std::string, void*> m_shaderIdentifierByName;

    RayTracingPipelineImpl(Device* device, const RayTracingPipelineDesc& desc);

    // IRayTracingPipeline implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

#if SLANG_RHI_ENABLE_AGILITY_SDK

class WorkGraphPipelineImpl : public WorkGraphPipeline
{
public:
    RefPtr<RootShaderObjectLayoutImpl> m_rootObjectLayout;
    ComPtr<ID3D12StateObject> m_stateObject;
    std::wstring m_programName;
    // Work graph index within the state object (from ID3D12WorkGraphProperties).
    uint32_t m_workGraphIndex = 0;
    uint64_t m_backingStoreMinBytes = 0;
    uint64_t m_backingStoreMaxBytes = 0;

    WorkGraphPipelineImpl(Device* device, const WorkGraphPipelineDesc& desc);

    // IWorkGraphPipeline implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getWorkGraphMemoryRequirements(
        WorkGraphMemoryRequirements* outRequirements
    ) override;
};

#endif // SLANG_RHI_ENABLE_AGILITY_SDK

} // namespace rhi::d3d12
