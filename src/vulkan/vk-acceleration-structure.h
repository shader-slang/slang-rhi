#pragma once

#include "vk-base.h"

namespace rhi::vk {

class AccelerationStructureImpl : public AccelerationStructure
{
public:
    VkAccelerationStructureKHR m_vkHandle = VK_NULL_HANDLE;
    RefPtr<BufferImpl> m_buffer;
    DescriptorHandle m_descriptorHandle;

public:
    AccelerationStructureImpl(Device* device, const AccelerationStructureDesc& desc);
    ~AccelerationStructureImpl();

    // IAccelerationStructure implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
    virtual SLANG_NO_THROW AccelerationStructureHandle getHandle() override;
    virtual SLANG_NO_THROW DeviceAddress SLANG_MCALL getDeviceAddress() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getDescriptorHandle(DescriptorHandle* outHandle) override;
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

    VkBuildAccelerationStructureFlagsKHR translateBuildFlags(AccelerationStructureBuildFlags flags);
    VkGeometryFlagsKHR translateGeometryFlags(AccelerationStructureGeometryFlags flags);
    VkRayTracingLssIndexingModeNV translateIndexingMode(LinearSweptSpheresIndexingMode mode);
    VkRayTracingLssPrimitiveEndCapsModeNV translateEndCapsMode(LinearSweptSpheresEndCapsMode mode);
};

} // namespace rhi::vk
