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
    {1, 1, 1}, // Texture1D
    {1, 1, 1}, // Texture1DArray
    {2, 2, 1}, // Texture2D
    {2, 2, 1}, // Texture2DArray
    {2, 2, 1}, // Texture2DMS
    {2, 2, 1}, // Texture2DMSArray
    {3, 3, 1}, // Texture3D
    {2, 2, 6}, // TextureCube
    {2, 2, 6}, // TextureCubeArray
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

        set(Format::RGBA32Uint, &_unpackUInt32Texel<4>);

        set(Format::RGBA32Float, &_unpackFloatTexel<4>);
        set(Format::RGB32Float, &_unpackFloatTexel<3>);

        set(Format::RG32Float, &_unpackFloatTexel<2>);
        set(Format::R32Float, &_unpackFloatTexel<1>);

        set(Format::RGBA16Float, &_unpackFloat16Texel<4>);
        set(Format::RG16Float, &_unpackFloat16Texel<2>);
        set(Format::R16Float, &_unpackFloat16Texel<1>);

        set(Format::RGBA8Unorm, &_unpackUnorm8Texel<4>);
        set(Format::BGRA8Unorm, &_unpackUnormBGRA8Texel);
        set(Format::R16Uint, &_unpackUInt16Texel<1>);
        set(Format::R32Uint, &_unpackUInt32Texel<1>);
        set(Format::D32Float, &_unpackFloatTexel<1>);
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
    TextureImpl(Device* device, const TextureDesc& desc);
    ~TextureImpl();

    Result init(const SubresourceData* initData);

    virtual SLANG_NO_THROW Result SLANG_MCALL getDefaultView(ITextureView** outTextureView) override;

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
        int64_t pitches[kMaxRank + 1];
        int64_t offset;
    };
    std::vector<MipLevel> m_mipLevels;
    void* m_data = nullptr;

    RefPtr<TextureViewImpl> m_defaultView;
};

class TextureViewImpl : public TextureView, public slang_prelude::IRWTexture
{
public:
    TextureViewImpl(Device* device, const TextureViewDesc& desc);

    virtual void makeExternal() override { m_texture.establishStrongReference(); }
    virtual void makeInternal() override { m_texture.breakStrongReference(); }

    // ITextureView implementation
    virtual SLANG_NO_THROW rhi::ITexture* SLANG_MCALL getTexture() override { return m_texture; }

    //
    // ITexture interface
    //

    slang_prelude::TextureDimensions GetDimensions(int mip = -1) override;

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
    BreakableReference<TextureImpl> m_texture;

    void* _getTexelPtr(const int32_t* texelCoords);
};

} // namespace rhi::cpu
