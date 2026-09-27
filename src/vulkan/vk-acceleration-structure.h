#pragma once

#include "vk-base.h"

namespace rhi::vk {

class AccelerationStructureImpl : public AccelerationStructure
{
public:
    AccelerationStructureImpl(Device* device, const AccelerationStructureDesc& desc);
    ~AccelerationStructureImpl();

    virtual void deleteThis() override;

    // IResource implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;

    // IAccelerationStructure implementation
    virtual SLANG_NO_THROW AccelerationStructureHandle getHandle() override;
    virtual SLANG_NO_THROW DeviceAddress SLANG_MCALL getDeviceAddress() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getDescriptorHandle(DescriptorHandle* outHandle) override;

public:
    DeviceAddress getAccelerationStructureDeviceAddress();

    VkAccelerationStructureKHR m_vkHandle = VK_NULL_HANDLE;
    RefPtr<BufferImpl> m_buffer;
    DeviceAddress m_deviceAddress = 0;
    DescriptorHandle m_descriptorHandle;
};

class MicromapImpl : public Micromap
{
public:
    MicromapImpl(Device* device, const MicromapDesc& desc);
    ~MicromapImpl();

    virtual void deleteThis() override;

    // IResource implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;

    // IMicromap implementation
    virtual SLANG_NO_THROW DeviceAddress SLANG_MCALL getDeviceAddress() override;

public:
    VkMicromapEXT m_vkHandle = VK_NULL_HANDLE;
    RefPtr<BufferImpl> m_buffer;
};

struct AccelerationStructureBuildDescConverter
{
public:
    VkAccelerationStructureBuildGeometryInfoKHR buildInfo = {
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR
    };
    std::vector<uint32_t> primitiveCounts;

    Result convert(const AccelerationStructureBuildDesc& buildDesc, IDebugCallback* debugCallback);

private:
    std::vector<VkAccelerationStructureGeometryKHR> geometries;
    std::vector<VkAccelerationStructureGeometrySpheresDataNV> spheresDatas;
    std::vector<VkAccelerationStructureGeometryLinearSweptSpheresDataNV> linearSweptSpheresDatas;
    std::vector<VkAccelerationStructureGeometryMotionTrianglesDataNV> motionTrianglesDatas;
    std::vector<VkAccelerationStructureTrianglesOpacityMicromapEXT> opacityMicromapDatas;
    std::vector<std::vector<VkMicromapUsageEXT>> opacityMicromapUsageCounts;

    VkBuildAccelerationStructureFlagsKHR translateBuildFlags(AccelerationStructureBuildFlags flags);
    VkGeometryFlagsKHR translateGeometryFlags(AccelerationStructureGeometryFlags flags);
    VkRayTracingLssIndexingModeNV translateIndexingMode(LinearSweptSpheresIndexingMode mode);
    VkRayTracingLssPrimitiveEndCapsModeNV translateEndCapsMode(LinearSweptSpheresEndCapsMode mode);
};

struct MicromapBuildDescConverter
{
    VkMicromapBuildInfoEXT buildInfo = {VK_STRUCTURE_TYPE_MICROMAP_BUILD_INFO_EXT};
    std::vector<VkMicromapUsageEXT> usageCounts;
    Result convert(const MicromapBuildDesc& desc);
};

VkBuildAccelerationStructureFlagsKHR translateClusterOperationFlags(ClusterOperationFlags flags);
VkClusterAccelerationStructureTypeNV translateClusterOperationMoveType(ClusterOperationMoveType type);
VkClusterAccelerationStructureInputInfoNV translateClusterOperationParams(
    const ClusterOperationParams& params,
    VkClusterAccelerationStructureClustersBottomLevelInputNV& bottomLevelInput,
    VkClusterAccelerationStructureTriangleClusterInputNV& triangleClusterInput,
    VkClusterAccelerationStructureMoveObjectsInputNV& moveObjectsInput
);
VkClusterAccelerationStructureOpModeNV translateClusterOperationMode(ClusterOperationMode mode);

} // namespace rhi::vk
