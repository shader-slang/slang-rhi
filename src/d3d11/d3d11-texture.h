#pragma once

#include "d3d11-base.h"

namespace rhi::d3d11 {

class TextureResourceImpl : public TextureResource
{
public:
    typedef TextureResource Parent;

    TextureResourceImpl(const Desc& desc)
        : Parent(desc)
    {
    }
    ComPtr<ID3D11Resource> m_resource;
};

} // namespace rhi::d3d11
