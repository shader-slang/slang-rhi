#pragma once

#include "d3d12-base.h"

namespace rhi::d3d12 {

#if SLANG_RHI_DXR

class AccelerationStructureImpl : public AccelerationStructure
{
public:
    DeviceImpl* m_device;
    RefPtr<BufferImpl> m_buffer;
    CPUDescriptorAllocation m_descriptor;
    ComPtr<ID3D12Device5> m_device5;

public:
    AccelerationStructureImpl(DeviceImpl* device, const AccelerationStructureDesc& desc)
        : AccelerationStructure(desc)
        , m_device(device)
    {
    }

    ~AccelerationStructureImpl();

    // IAccelerationStructure implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
    virtual SLANG_NO_THROW AccelerationStructureHandle SLANG_MCALL getHandle() override;
    virtual SLANG_NO_THROW DeviceAddress SLANG_MCALL getDeviceAddress() override;
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
#endif // SLANG_RHI_ENABLE_NVAPI

#endif // SLANG_RHI_DXR

} // namespace rhi::d3d12
