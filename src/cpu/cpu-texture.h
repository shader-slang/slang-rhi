#pragma once

#include "cpu-base.h"

namespace rhi::cpu {

struct CPUTextureBaseShapeInfo
{
    int32_t rank;
    int32_t baseCoordCount;
    int32_t implicitArrayElementCount;
};

static const CPUTextureBaseShapeInfo kCPUTextureBaseShapeInfos[] = {
    /* Texture1D */ {1, 1, 1},
    /* Texture2D */ {2, 2, 1},
    /* Texture3D */ {3, 3, 1},
    /* TextureCube */ {2, 3, 6},
};

typedef void (*CPUTextureUnpackFunc)(const void* texelData, void* outData, size_t outSize);

struct CPUTextureFormatInfo
{
    CPUTextureUnpackFunc unpackFunc;
};

template<int N>
void _unpackFloatTexel(const void* texelData, void* outData, size_t outSize);

template<int N>
void _unpackFloat16Texel(const void* texelData, void* outData, size_t outSize);

inline float _unpackUnorm8Value(uint8_t value)
{
    return value / 255.0f;
}

template<int N>
void _unpackUnorm8Texel(const void* texelData, void* outData, size_t outSize);

void _unpackUnormBGRA8Texel(const void* texelData, void* outData, size_t outSize);

template<int N>
void _unpackUInt16Texel(const void* texelData, void* outData, size_t outSize);

template<int N>
void _unpackUInt32Texel(const void* texelData, void* outData, size_t outSize);

struct CPUFormatInfoMap
{
    CPUFormatInfoMap()
    {
        memset(m_infos, 0, sizeof(m_infos));

        set(Format::R32G32B32A32_FLOAT, &_unpackFloatTexel<4>);
        set(Format::R32G32B32_FLOAT, &_unpackFloatTexel<3>);

        set(Format::R32G32_FLOAT, &_unpackFloatTexel<2>);
        set(Format::R32_FLOAT, &_unpackFloatTexel<1>);

        set(Format::R16G16B16A16_FLOAT, &_unpackFloat16Texel<4>);
        set(Format::R16G16_FLOAT, &_unpackFloat16Texel<2>);
        set(Format::R16_FLOAT, &_unpackFloat16Texel<1>);

        set(Format::R8G8B8A8_UNORM, &_unpackUnorm8Texel<4>);
        set(Format::B8G8R8A8_UNORM, &_unpackUnormBGRA8Texel);
        set(Format::R16_UINT, &_unpackUInt16Texel<1>);
        set(Format::R32_UINT, &_unpackUInt32Texel<1>);
        set(Format::D32_FLOAT, &_unpackFloatTexel<1>);
    }

    void set(Format format, CPUTextureUnpackFunc func)
    {
        auto& info = m_infos[size_t(format)];
        info.unpackFunc = func;
    }

    SLANG_FORCE_INLINE const CPUTextureFormatInfo& get(Format format) const { return m_infos[size_t(format)]; }

    CPUTextureFormatInfo m_infos[size_t(Format::_Count)];
};

static const CPUFormatInfoMap g_formatInfoMap;

inline const CPUTextureFormatInfo* _getFormatInfo(Format format)
{
    const CPUTextureFormatInfo& info = g_formatInfoMap.get(format);
    return info.unpackFunc ? &info : nullptr;
}

class TextureImpl : public Texture
{
    enum
    {
        kMaxRank = 3
    };

public:
    TextureImpl(const TextureDesc& desc)
        : Texture(desc)
    {
    }

    ~TextureImpl();

    Result init(const SubresourceData* initData);

    const TextureDesc& _getDesc() { return m_desc; }
    Format getFormat() { return m_desc.format; }
    int32_t getRank() { return m_baseShape->rank; }

    const CPUTextureBaseShapeInfo* m_baseShape;
    const CPUTextureFormatInfo* m_formatInfo;
    int32_t m_effectiveArrayElementCount = 0;
    uint32_t m_texelSize = 0;

    struct MipLevel
    {
        int32_t extents[kMaxRank];
        int64_t strides[kMaxRank + 1];
        int64_t offset;
    };
    std::vector<MipLevel> m_mipLevels;
    void* m_data = nullptr;
};

class TextureViewImpl : public TextureView, public slang_prelude::IRWTexture
{
public:
    TextureViewImpl(const TextureViewDesc& desc)
        : TextureView(desc)
    {
    }

    //
    // ITexture interface
    //

    slang_prelude::TextureDimensions GetDimensions(int mipLevel = -1) override;

    void Load(const int32_t* texelCoords, void* outData, size_t dataSize) override;

    void Sample(slang_prelude::SamplerState samplerState, const float* coords, void* outData, size_t dataSize) override;

    void SampleLevel(
        slang_prelude::SamplerState samplerState,
        const float* coords,
        float level,
        void* outData,
        size_t dataSize
    ) override;

    //
    // IRWTexture interface
    //

    void* refAt(const uint32_t* texelCoords) override;

public:
    RefPtr<TextureImpl> m_texture;

    void* _getTexelPtr(const int32_t* texelCoords);
};

} // namespace rhi::cpu
