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
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS translateBuildFlags(AccelerationStructureBuildFlags flags)
    {
        static_assert(
            uint32_t(AccelerationStructureBuildFlags::None) == D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE
        );
        static_assert(
            uint32_t(AccelerationStructureBuildFlags::AllowUpdate) ==
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE
        );
        static_assert(
            uint32_t(AccelerationStructureBuildFlags::AllowCompaction) ==
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION
        );
        static_assert(
            uint32_t(AccelerationStructureBuildFlags::PreferFastTrace) ==
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE
        );
        static_assert(
            uint32_t(AccelerationStructureBuildFlags::PreferFastBuild) ==
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD
        );
        static_assert(
            uint32_t(AccelerationStructureBuildFlags::MinimizeMemory) ==
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_MINIMIZE_MEMORY
        );
        return (D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS)flags;
    }
    D3D12_RAYTRACING_GEOMETRY_FLAGS translateGeometryFlags(AccelerationStructureGeometryFlags flags)
    {
        static_assert(uint32_t(AccelerationStructureGeometryFlags::None) == D3D12_RAYTRACING_GEOMETRY_FLAG_NONE);
        static_assert(uint32_t(AccelerationStructureGeometryFlags::Opaque) == D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE);
        static_assert(
            uint32_t(AccelerationStructureGeometryFlags::NoDuplicateAnyHitInvocation) ==
            D3D12_RAYTRACING_GEOMETRY_FLAG_NO_DUPLICATE_ANYHIT_INVOCATION
        );
        return (D3D12_RAYTRACING_GEOMETRY_FLAGS)flags;
    }
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
    )
    {
        static_assert(
            uint32_t(AccelerationStructureBuildFlags::None) ==
            NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE_EX
        );
        static_assert(
            uint32_t(AccelerationStructureBuildFlags::AllowUpdate) ==
            NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE_EX
        );
        static_assert(
            uint32_t(AccelerationStructureBuildFlags::AllowCompaction) ==
            NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION_EX
        );
        static_assert(
            uint32_t(AccelerationStructureBuildFlags::PreferFastTrace) ==
            NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE_EX
        );
        static_assert(
            uint32_t(AccelerationStructureBuildFlags::PreferFastBuild) ==
            NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD_EX
        );
        static_assert(
            uint32_t(AccelerationStructureBuildFlags::MinimizeMemory) ==
            NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_MINIMIZE_MEMORY_EX
        );
        return (NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS_EX)flags;
    }
    D3D12_RAYTRACING_GEOMETRY_FLAGS translateGeometryFlags(AccelerationStructureGeometryFlags flags)
    {
        static_assert(uint32_t(AccelerationStructureGeometryFlags::None) == D3D12_RAYTRACING_GEOMETRY_FLAG_NONE);
        static_assert(uint32_t(AccelerationStructureGeometryFlags::Opaque) == D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE);
        static_assert(
            uint32_t(AccelerationStructureGeometryFlags::NoDuplicateAnyHitInvocation) ==
            D3D12_RAYTRACING_GEOMETRY_FLAG_NO_DUPLICATE_ANYHIT_INVOCATION
        );
        return (D3D12_RAYTRACING_GEOMETRY_FLAGS)flags;
    }
};
#endif // SLANG_RHI_ENABLE_NVAPI

#endif // SLANG_RHI_DXR

} // namespace rhi::d3d12
