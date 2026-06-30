#pragma once

#include "cuda-base.h"

#include "core/stable_vector.h"

namespace rhi::cuda {

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
    CUdeviceptr m_buffer = 0;
    CUdeviceptr m_propertyBuffer = 0;
    optix::OptixTraversableHandle m_handle = 0;
};

} // namespace rhi::cuda
