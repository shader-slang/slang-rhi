#pragma once

#include "cuda-base.h"
#include "cuda-buffer.h"
#include "cuda-texture.h"

namespace rhi::cuda {

class TextureViewImpl : public TextureView
{
public:
    TextureViewImpl(RendererBase* device, const TextureViewDesc& desc)
        : TextureView(device, desc)
    {
    }

    RefPtr<TextureImpl> m_texture;
};

} // namespace rhi::cuda
