#pragma once

#include "d3d12-base.h"

namespace rhi::d3d12 {

class TextureResourceImpl : public TextureResource
{
public:
    typedef TextureResource Parent;

    TextureResourceImpl(const Desc& desc);

    ~TextureResourceImpl();

    D3D12Resource m_resource;
    D3D12_RESOURCE_STATES m_defaultState;

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeResourceHandle(InteropHandle* outHandle) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getSharedHandle(InteropHandle* outHandle) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL setDebugName(const char* name) override;
};

} // namespace rhi::d3d12
