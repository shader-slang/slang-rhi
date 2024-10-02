#pragma once

#include "metal-base.h"

namespace rhi::metal {

class AccelerationStructureImpl : public AccelerationStructure
{
public:
    DeviceImpl* m_device;
    NS::SharedPtr<MTL::AccelerationStructure> m_accelerationStructure;
    uint32_t m_globalIndex;

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

} // namespace rhi::metal
