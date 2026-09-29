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
    struct StructuralRecordInfo
    {
        /// True when this export has its own compiler-declared structural `Record`. A hit group can
        /// be false here while still having local root CBVs because DXR requires all hit groups that
        /// share a stage export to use the same local root signature.
        bool hasStructuralRecord = false;

        /// Number of application bytes described by the structural `Record` type. This is zero for
        /// a void or zero-sized Record, so `hasStructuralRecord` carries the semantic distinction.
        Size dataSize = 0;

        /// Number of root-CBV addresses stored after the shader identifier.
        ///
        /// A miss or callable export normally has one address. A composed hit-group export can
        /// require more than one when its stages were compiled with different reserved cbuffer
        /// bindings. Every address points at the same application-data allocation.
        uint32_t localRootCBVCount = 0;
    };

    RefPtr<RootShaderObjectLayoutImpl> m_rootObjectLayout;
    ComPtr<ID3D12StateObject> m_stateObject;
    std::map<std::string, void*> m_shaderIdentifierByName;

    /// Keeps the local root signatures used by structural shader-record exports alive for the
    /// lifetime of the state object.
    std::vector<ComPtr<ID3D12RootSignature>> m_localRootSignatures;

    /// Gives the reflected application-data and native local-root shape for each affected
    /// shader-table export.
    ///
    /// Miss and callable entries use their stage export names. Hit-group entries use the
    /// application-provided hit-group export name because that is the identifier written into the
    /// shader table. An export with its own structural Record stores those bytes in an RHI-owned
    /// constant buffer and puts its 64-bit GPU address once for each local root CBV in the native
    /// shader record. An inherited-only hit group keeps legacy application bytes inline.
    std::map<std::string, StructuralRecordInfo> m_structuralRecordInfoByName;

    RayTracingPipelineImpl(Device* device, const RayTracingPipelineDesc& desc);

    /// Returns the structural/local-root contract for an export, or null when it has neither.
    const StructuralRecordInfo* findStructuralRecordInfo(const std::string& name) const
    {
        auto it = m_structuralRecordInfoByName.find(name);
        return it != m_structuralRecordInfoByName.end() ? &it->second : nullptr;
    }

    // IRayTracingPipeline implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

} // namespace rhi::d3d12
