#pragma once

#include "vk-base.h"

namespace rhi::vk {

class AccelerationStructureImpl : public AccelerationStructure
{
public:
    RefPtr<DeviceImpl> m_device;
    VkAccelerationStructureKHR m_vkHandle = VK_NULL_HANDLE;
    RefPtr<BufferImpl> m_buffer;

public:
    AccelerationStructureImpl(DeviceImpl* device, const AccelerationStructureDesc& desc)
        : AccelerationStructure(desc)
        , m_device(device)
    {
    }

    ~AccelerationStructureImpl();

    // IAccelerationStructure implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
    virtual SLANG_NO_THROW AccelerationStructureHandle getHandle() override;
    virtual SLANG_NO_THROW DeviceAddress SLANG_MCALL getDeviceAddress() override;
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

    VkBuildAccelerationStructureFlagsKHR translateBuildFlags(AccelerationStructureBuildFlags flags)
    {
        VkBuildAccelerationStructureFlagsKHR result = VkBuildAccelerationStructureFlagsKHR(0);
        if (is_set(flags, AccelerationStructureBuildFlags::AllowCompaction))
        {
            result |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR;
        }
        if (is_set(flags, AccelerationStructureBuildFlags::AllowUpdate))
        {
            result |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
        }
        if (is_set(flags, AccelerationStructureBuildFlags::MinimizeMemory))
        {
            result |= VK_BUILD_ACCELERATION_STRUCTURE_LOW_MEMORY_BIT_KHR;
        }
        if (is_set(flags, AccelerationStructureBuildFlags::PreferFastBuild))
        {
            result |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
        }
        if (is_set(flags, AccelerationStructureBuildFlags::PreferFastTrace))
        {
            result |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        }
        return result;
    }

    VkGeometryFlagsKHR translateGeometryFlags(AccelerationStructureGeometryFlags flags)
    {
        VkGeometryFlagsKHR result = VkGeometryFlagsKHR(0);
        if (is_set(flags, AccelerationStructureGeometryFlags::Opaque))
            result |= VK_GEOMETRY_OPAQUE_BIT_KHR;
        if (is_set(flags, AccelerationStructureGeometryFlags::NoDuplicateAnyHitInvocation))
            result |= VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR;
        return result;
    }

    VkRayTracingLssIndexingModeNV translateIndexingMode(LinearSweptSpheresIndexingMode mode)
    {
        switch (mode)
        {
        case LinearSweptSpheresIndexingMode::List:
            return VK_RAY_TRACING_LSS_INDEXING_MODE_LIST_NV;
        case LinearSweptSpheresIndexingMode::Successive:
            return VK_RAY_TRACING_LSS_INDEXING_MODE_SUCCESSIVE_NV;
        default:
            return VkRayTracingLssIndexingModeNV(0);
        }
    }

    VkRayTracingLssPrimitiveEndCapsModeNV translateEndCapsMode(LinearSweptSpheresEndCapsMode mode)
    {
        switch (mode)
        {
        case LinearSweptSpheresEndCapsMode::None:
            return VK_RAY_TRACING_LSS_PRIMITIVE_END_CAPS_MODE_NONE_NV;
        case LinearSweptSpheresEndCapsMode::Chained:
            return VK_RAY_TRACING_LSS_PRIMITIVE_END_CAPS_MODE_CHAINED_NV;
        default:
            return VkRayTracingLssPrimitiveEndCapsModeNV(0);
        }
    }
};

} // namespace rhi::vk
