#pragma once

#include "cuda-base.h"
#include "cuda-buffer.h"
#include "cuda-texture.h"

namespace rhi::cuda {

class TextureViewImpl : public TextureView
{
public:
    TextureViewImpl(const TextureViewDesc& desc)
        : TextureView(desc)
    {
    }

    RefPtr<TextureImpl> m_texture;
};

} // namespace rhi::cuda
