#pragma once

#include "d3d12-base.h"

namespace rhi::d3d12 {

inline bool usesOpacityMicromaps(const AccelerationStructureBuildDesc& desc)
{
    for (uint32_t i = 0; i < desc.inputCount; ++i)
        if (desc.inputs[i].type == AccelerationStructureBuildInputType::Triangles &&
            findStructInChain<AccelerationStructureOpacityMicromapDesc>(desc.inputs[i].triangles.next))
            return true;
    return false;
}

class AccelerationStructureImpl : public AccelerationStructure
{
public:
    AccelerationStructureImpl(Device* device, const AccelerationStructureDesc& desc);
    ~AccelerationStructureImpl();

    virtual void deleteThis() override;

    // IResource implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;

    // IAccelerationStructure implementation
    virtual SLANG_NO_THROW AccelerationStructureHandle SLANG_MCALL getHandle() override;
    virtual SLANG_NO_THROW DeviceAddress SLANG_MCALL getDeviceAddress() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getDescriptorHandle(DescriptorHandle* outHandle) override;

public:
    RefPtr<BufferImpl> m_buffer;
    CPUDescriptorAllocation m_descriptor;
    DescriptorHandle m_descriptorHandle;
};

class MicromapImpl : public Micromap
{
public:
    MicromapImpl(Device* device, const MicromapDesc& desc);

    virtual void deleteThis() override;

    // IResource implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;

    // IMicromap implementation
    virtual SLANG_NO_THROW DeviceAddress SLANG_MCALL getDeviceAddress() override;

public:
    RefPtr<BufferImpl> m_buffer;
};

struct AccelerationStructureBuildDescConverter
{
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS desc = {};
    std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geomDescs;
    std::vector<D3D12_RAYTRACING_GEOMETRY_TRIANGLES_DESC> triangleDescs;
    std::vector<D3D12_RAYTRACING_GEOMETRY_OMM_LINKAGE_DESC> ommLinkages;
    std::vector<D3D12_RAYTRACING_GEOMETRY_OMM_TRIANGLES_DESC> ommTriangleDescs;
    Result convert(const AccelerationStructureBuildDesc& buildDesc, IDebugCallback* callback);

private:
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS translateBuildFlags(AccelerationStructureBuildFlags flags);
    D3D12_RAYTRACING_GEOMETRY_FLAGS translateGeometryFlags(AccelerationStructureGeometryFlags flags);
};

struct MicromapBuildDescConverter
{
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS desc = {};
    D3D12_RAYTRACING_OPACITY_MICROMAP_ARRAY_DESC arrayDesc = {};
    std::vector<D3D12_RAYTRACING_OPACITY_MICROMAP_HISTOGRAM_ENTRY> histogram;
    Result convert(const MicromapBuildDesc& buildDesc);
};

#if SLANG_RHI_ENABLE_NVAPI
struct AccelerationStructureBuildDescConverterNVAPI
{
    NVAPI_D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS_EX desc = {};
    std::vector<NVAPI_D3D12_RAYTRACING_GEOMETRY_DESC_EX> geomDescs;
    Result convert(const AccelerationStructureBuildDesc& buildDesc, IDebugCallback* callback);

private:
    NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS_EX translateBuildFlags(
        AccelerationStructureBuildFlags flags
    );
    D3D12_RAYTRACING_GEOMETRY_FLAGS translateGeometryFlags(AccelerationStructureGeometryFlags flags);
    NVAPI_D3D12_RAYTRACING_LSS_PRIMITIVE_FORMAT translateIndexingMode(LinearSweptSpheresIndexingMode mode);
    NVAPI_D3D12_RAYTRACING_LSS_ENDCAP_MODE translateEndCapsMode(LinearSweptSpheresEndCapsMode mode);
};

NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_FLAGS translateClusterOperationFlags(
    ClusterOperationFlags flags
);
NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_MOVE_TYPE translateClusterOperationMoveType(
    ClusterOperationMoveType type
);
NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INPUTS translateClusterOperationParams(
    const ClusterOperationParams& params
);
NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_MODE translateClusterOperationMode(ClusterOperationMode mode);

#endif // SLANG_RHI_ENABLE_NVAPI

} // namespace rhi::d3d12
