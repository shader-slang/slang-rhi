#pragma once

#include "d3d12-base.h"

namespace rhi::d3d12 {

class AccelerationStructureImpl : public AccelerationStructure
{
public:
    RefPtr<BufferImpl> m_buffer;
    CPUDescriptorAllocation m_descriptor;
    DescriptorHandle m_descriptorHandle;

public:
    AccelerationStructureImpl(Device* device, const AccelerationStructureDesc& desc);
    ~AccelerationStructureImpl();

    // IAccelerationStructure implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
    virtual SLANG_NO_THROW AccelerationStructureHandle SLANG_MCALL getHandle() override;
    virtual SLANG_NO_THROW DeviceAddress SLANG_MCALL getDeviceAddress() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getDescriptorHandle(DescriptorHandle* outHandle) override;
};

struct AccelerationStructureBuildDescConverter
{
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS desc = {};
    std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geomDescs;
    Result convert(const AccelerationStructureBuildDesc& buildDesc, IDebugCallback* callback);

private:
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS translateBuildFlags(AccelerationStructureBuildFlags flags);
    D3D12_RAYTRACING_GEOMETRY_FLAGS translateGeometryFlags(AccelerationStructureGeometryFlags flags);
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
