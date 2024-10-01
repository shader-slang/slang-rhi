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
    TextureViewImpl(const TextureViewDesc& desc)
        : TextureView(desc)
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

} // namespace rhi::d3d12
