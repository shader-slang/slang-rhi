#pragma once

#include "d3d12-base.h"

namespace rhi::d3d12 {

class TextureImpl : public Texture
{
public:
    typedef Texture Parent;

    TextureImpl(const TextureDesc& desc);

    ~TextureImpl();

    D3D12Resource m_resource;
    D3D12_RESOURCE_STATES m_defaultState;

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getSharedHandle(NativeHandle* outHandle) override;
};

} // namespace rhi::d3d12
