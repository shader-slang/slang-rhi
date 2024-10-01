#pragma once

#include "vk-base.h"
#include "vk-buffer.h"
#include "vk-device.h"
#include "vk-texture.h"

namespace rhi::vk {

class TextureViewImpl : public TextureView
{
public:
    TextureViewImpl(const TextureViewDesc& desc)
        : TextureView(desc)
    {
    }

    RefPtr<TextureImpl> m_texture;

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;

    TextureSubresourceView getView();
};

} // namespace rhi::vk
