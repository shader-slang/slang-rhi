#pragma once

#include "d3d12-base.h"

namespace rhi::d3d12 {

class BufferImpl : public Buffer
{
public:
    typedef Buffer Parent;

    BufferImpl(const BufferDesc& desc);

    ~BufferImpl();

    /// The resource in gpu memory, allocated on the correct heap relative to the cpu access flag
    D3D12Resource m_resource;

    D3D12_RESOURCE_STATES m_defaultState;

    virtual SLANG_NO_THROW DeviceAddress SLANG_MCALL getDeviceAddress() override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getSharedHandle(NativeHandle* outHandle) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL map(MemoryRange* rangeToRead, void** outPointer) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL unmap(MemoryRange* writtenRange) override;
};

} // namespace rhi::d3d12
