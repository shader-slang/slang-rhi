#pragma once

#include "cpu-base.h"
#include "cpu-buffer.h"
#include "cpu-texture.h"

namespace rhi::cpu {

class TextureViewImpl : public TextureView, public slang_prelude::IRWTexture
{
public:
    TextureViewImpl(Device* device, const TextureViewDesc& desc)
        : TextureView(device, desc)
    {
    }

    //
    // ITexture interface
    //

    slang_prelude::TextureDimensions GetDimensions(int mipLevel = -1) SLANG_OVERRIDE;

    void Load(const int32_t* texelCoords, void* outData, size_t dataSize) SLANG_OVERRIDE;

    void Sample(slang_prelude::SamplerState samplerState, const float* coords, void* outData, size_t dataSize)
        SLANG_OVERRIDE;

    void SampleLevel(
        slang_prelude::SamplerState samplerState,
        const float* coords,
        float level,
        void* outData,
        size_t dataSize
    ) SLANG_OVERRIDE;

    //
    // IRWTexture interface
    //

    void* refAt(const uint32_t* texelCoords) SLANG_OVERRIDE;

public:
    RefPtr<TextureImpl> m_texture;

    void* _getTexelPtr(int32_t const* texelCoords);
};

} // namespace rhi::cpu
