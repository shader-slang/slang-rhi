#pragma once

#include "cuda-base.h"

#include "core/stable_vector.h"

namespace rhi::cuda {

class AccelerationStructureImpl : public AccelerationStructure
{
public:
    CUdeviceptr m_buffer;
    CUdeviceptr m_propertyBuffer;
    optix::OptixTraversableHandle m_handle;

public:
    AccelerationStructureImpl(Device* device, const AccelerationStructureDesc& desc);
    ~AccelerationStructureImpl();

    // IAccelerationStructure implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
    virtual SLANG_NO_THROW AccelerationStructureHandle getHandle() override;
    virtual SLANG_NO_THROW DeviceAddress SLANG_MCALL getDeviceAddress() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getDescriptorHandle(DescriptorHandle* outHandle) override;
};

} // namespace rhi::cuda
