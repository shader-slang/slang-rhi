#pragma once

#include "cpu-base.h"

#include <memory>

namespace rhi::cpu {

class AccelerationStructureImpl : public AccelerationStructure, public slang_prelude::IRaytracingAccelerationStructure
{
public:
    AccelerationStructureImpl(Device* device, const AccelerationStructureDesc& desc);
    ~AccelerationStructureImpl();

    // IResource implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;

    // IAccelerationStructure implementation
    virtual SLANG_NO_THROW AccelerationStructureHandle SLANG_MCALL getHandle() override;
    virtual SLANG_NO_THROW DeviceAddress SLANG_MCALL getDeviceAddress() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getDescriptorHandle(DescriptorHandle* outHandle) override;

    // IRaytracingAccelerationStructure implementation
    virtual bool proceed(slang_prelude::RayQueryState* state) const override;

    Result build(const AccelerationStructureBuildDesc& desc);
    Result copyFrom(const AccelerationStructureImpl& source);

private:
    struct Impl;
    std::shared_ptr<Impl> m_impl;
};

} // namespace rhi::cpu
