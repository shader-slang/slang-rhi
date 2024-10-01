#pragma once

#include "vk-base.h"

namespace rhi::vk {

class AccelerationStructureImpl : public AccelerationStructure
{
public:
    DeviceImpl* m_device;
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

} // namespace rhi::vk
