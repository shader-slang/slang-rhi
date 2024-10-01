#pragma once

#include "d3d12-base.h"

namespace rhi::d3d12 {

#if SLANG_RHI_DXR

class AccelerationStructureImpl : public AccelerationStructure
{
public:
    DeviceImpl* m_device;
    RefPtr<BufferImpl> m_buffer;
    D3D12Descriptor m_descriptor;
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

#endif // SLANG_RHI_DXR

} // namespace rhi::d3d12
