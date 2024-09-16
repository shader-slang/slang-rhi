#pragma once

#include "../d3d/d3d-util.h"
#include "d3d12-base.h"
#include "d3d12-buffer.h"
#include "d3d12-texture.h"

#include <map>

namespace rhi::d3d12 {

class TextureViewImpl : public TextureView
{
public:
    TextureViewImpl(RendererBase* device, const TextureViewDesc& desc)
        : TextureView(device, desc)
    {
    }

    RefPtr<TextureImpl> m_texture;

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;

    D3D12Descriptor getSRV();
    D3D12Descriptor getUAV();
    D3D12Descriptor getRTV();
    D3D12Descriptor getDSV();

private:
    D3D12Descriptor m_srv = {};
    D3D12Descriptor m_uav = {};
    D3D12Descriptor m_rtv = {};
    D3D12Descriptor m_dsv = {};
};

#if SLANG_RHI_DXR

class AccelerationStructureImpl : public AccelerationStructureBase
{
public:
    RefPtr<BufferImpl> m_buffer;
    uint64_t m_offset;
    uint64_t m_size;
    D3D12Descriptor m_descriptor;
    ComPtr<ID3D12Device5> m_device5;

public:
    AccelerationStructureImpl(RendererBase* device, const IAccelerationStructure::CreateDesc& desc)
        : AccelerationStructureBase(device, desc)
    {
    }

    ~AccelerationStructureImpl();

    virtual SLANG_NO_THROW DeviceAddress SLANG_MCALL getDeviceAddress() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

#endif // SLANG_RHI_DXR

} // namespace rhi::d3d12
