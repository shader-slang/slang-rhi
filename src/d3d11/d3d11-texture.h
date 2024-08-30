#pragma once

#include "d3d11-base.h"

namespace rhi::d3d11 {

class TextureImpl : public Texture
{
public:
    typedef Texture Parent;

    TextureImpl(const TextureDesc& desc)
        : Parent(desc)
    {
    }
    ComPtr<ID3D11Resource> m_resource;
};

} // namespace rhi::d3d11
