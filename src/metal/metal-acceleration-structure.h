#pragma once

#include "metal-base.h"

namespace rhi::metal {

class AccelerationStructureImpl : public AccelerationStructure
{
public:
    NS::SharedPtr<MTL::AccelerationStructure> m_accelerationStructure;
    uint32_t m_globalIndex;

public:
    AccelerationStructureImpl(Device* device, const AccelerationStructureDesc& desc);
    ~AccelerationStructureImpl();

    // IAccelerationStructure implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
    virtual SLANG_NO_THROW AccelerationStructureHandle getHandle() override;
    virtual SLANG_NO_THROW DeviceAddress SLANG_MCALL getDeviceAddress() override;
};

struct AccelerationStructureBuildDescConverter
{
public:
    NS::SharedPtr<MTL::AccelerationStructureDescriptor> descriptor;

    Result convert(
        const AccelerationStructureBuildDesc& buildDesc,
        const NS::Array* accelerationStructureArray,
        IDebugCallback* debugCallback
    );

private:
    MTL::AccelerationStructureUsage translateBuildFlags(AccelerationStructureBuildFlags flags)
    {
        MTL::AccelerationStructureUsage result = MTL::AccelerationStructureUsageNone;
        // if (is_set(flags, AccelerationStructureBuildFlags::AllowCompaction)) {}
        if (is_set(flags, AccelerationStructureBuildFlags::AllowUpdate))
        {
            result |= MTL::AccelerationStructureUsageRefit;
        }
        if (is_set(flags, AccelerationStructureBuildFlags::MinimizeMemory))
        {
            result |= MTL::AccelerationStructureUsageExtendedLimits;
        }
        if (is_set(flags, AccelerationStructureBuildFlags::PreferFastBuild))
        {
            result |= MTL::AccelerationStructureUsagePreferFastBuild;
        }
        // if (is_set(flags, AccelerationStructureBuildFlags::PreferFastTrace)) {}
        return result;
    }
};

} // namespace rhi::metal
