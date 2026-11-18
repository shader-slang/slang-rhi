#include "cpu-texture.h"
#include "cpu-device.h"

#include <array>

namespace rhi::cpu {

inline const CPUTextureBaseShapeInfo* _getBaseShapeInfo(TextureType baseShape)
{
    return &kCPUTextureBaseShapeInfos[(int)baseShape];
}

// Whether the type carries an explicit array coordinate, in addition to the implicit array of
// cube faces that cube textures have (see kCPUTextureBaseShapeInfos).
static bool _isTextureArrayType(TextureType type)
{
    switch (type)
    {
    case TextureType::Texture1DArray:
    case TextureType::Texture2DArray:
    case TextureType::Texture2DMSArray:
    case TextureType::TextureCubeArray:
        return true;
    default:
        return false;
    }
}

// A cube array's element index combines the cube face and the cube index. Each coordinate is
// clamped against its own dimension before combining, so the multiply cannot overflow int32_t
// for large coordinates; the float-to-int conversion of a coordinate is undefined by the
// standard for values outside int32_t (saturating on the targets this backend runs on), and
// the clamps below bound whatever it produces.
static int32_t _combineCubeArrayElementIndex(
    int32_t face,
    int32_t cubeIndex,
    const CPUTextureBaseShapeInfo* baseShape,
    uint32_t arrayLength
)
{
    if (face >= baseShape->implicitArrayElementCount)
        face = baseShape->implicitArrayElementCount - 1;
    if (face < 0)
        face = 0;
    if (cubeIndex >= int32_t(arrayLength))
        cubeIndex = int32_t(arrayLength) - 1;
    if (cubeIndex < 0)
        cubeIndex = 0;
    return face + cubeIndex * baseShape->implicitArrayElementCount;
}

template<int N>
void _unpackFloatTexel(const void* texelData, void* outData, size_t outSize)
{
    auto input = (const float*)texelData;

    float temp[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    for (int i = 0; i < N; ++i)
        temp[i] = input[i];

    memcpy(outData, temp, outSize);
}

template<int N>
void _unpackFloat16Texel(const void* texelData, void* outData, size_t outSize)
{
    auto input = (const int16_t*)texelData;

    float temp[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    for (int i = 0; i < N; ++i)
        temp[i] = math::halfToFloat(input[i]);

    memcpy(outData, temp, outSize);
}

template<int N>
void _unpackUnorm8Texel(const void* texelData, void* outData, size_t outSize)
{
    auto input = (const uint8_t*)texelData;

    float temp[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    for (int i = 0; i < N; ++i)
        temp[i] = _unpackUnorm8Value(input[i]);

    memcpy(outData, temp, outSize);
}

void _unpackUnormBGRA8Texel(const void* texelData, void* outData, size_t outSize)
{
    auto input = (const uint8_t*)texelData;

    float temp[4];
    temp[0] = _unpackUnorm8Value(input[2]);
    temp[1] = _unpackUnorm8Value(input[1]);
    temp[2] = _unpackUnorm8Value(input[0]);
    temp[3] = _unpackUnorm8Value(input[3]);

    memcpy(outData, temp, outSize);
}

template<int N>
void _unpackUInt16Texel(const void* texelData, void* outData, size_t outSize)
{
    auto input = (const uint16_t*)texelData;

    uint32_t temp[4] = {0, 0, 0, 0};
    for (int i = 0; i < N; ++i)
        temp[i] = input[i];

    memcpy(outData, temp, outSize);
}

template<int N>
void _unpackUInt32Texel(const void* texelData, void* outData, size_t outSize)
{
    auto input = (const uint32_t*)texelData;

    uint32_t temp[4] = {0, 0, 0, 0};
    for (int i = 0; i < N; ++i)
        temp[i] = input[i];

    memcpy(outData, temp, outSize);
}

static constexpr auto makeFormatInfoMap()
{
    std::array<CPUTextureFormatInfo, size_t(Format::_Count)> infos{};

    infos[size_t(Format::RGBA32Uint)] = {&_unpackUInt32Texel<4>};

    infos[size_t(Format::RGBA32Float)] = {&_unpackFloatTexel<4>};
    infos[size_t(Format::RGB32Float)] = {&_unpackFloatTexel<3>};

    infos[size_t(Format::RG32Float)] = {&_unpackFloatTexel<2>};
    infos[size_t(Format::R32Float)] = {&_unpackFloatTexel<1>};

    infos[size_t(Format::RGBA16Float)] = {&_unpackFloat16Texel<4>};
    infos[size_t(Format::RG16Float)] = {&_unpackFloat16Texel<2>};
    infos[size_t(Format::R16Float)] = {&_unpackFloat16Texel<1>};

    infos[size_t(Format::RGBA8Unorm)] = {&_unpackUnorm8Texel<4>};
    infos[size_t(Format::BGRA8Unorm)] = {&_unpackUnormBGRA8Texel};
    infos[size_t(Format::R16Uint)] = {&_unpackUInt16Texel<1>};
    infos[size_t(Format::R32Uint)] = {&_unpackUInt32Texel<1>};
    infos[size_t(Format::D32Float)] = {&_unpackFloatTexel<1>};

    return infos;
}

static constexpr auto g_formatInfoMap = makeFormatInfoMap();

const CPUTextureFormatInfo* _getFormatInfo(Format format)
{
    if (size_t(format) >= size_t(Format::_Count))
        return nullptr;
    const CPUTextureFormatInfo& info = g_formatInfoMap[size_t(format)];
    return info.unpackFunc ? &info : nullptr;
}

TextureImpl::TextureImpl(Device* device, const TextureDesc& desc)
    : Texture(device, desc)
{
}

TextureImpl::~TextureImpl()
{
    destroyDefaultView();
    free(m_data);
}

Result TextureImpl::init(const SubresourceData* initData)
{
    auto desc = m_desc;

    // The format of the texture will determine the
    // size of the texels we allocate.
    //
    // TODO: Compressed formats usually work in terms
    // of a fixed block size, so that we cannot actually
    // compute a simple `texelSize` like this. Instead
    // we should be computing a `blockSize` and then
    // a `blockExtents` value that gives the extent
    // in texels of each block. For uncompressed formats
    // the block extents would be 1 along each axis.
    //
    auto format = desc.format;
    const FormatInfo& texelInfo = getFormatInfo(format);
    uint32_t texelSize = uint32_t(texelInfo.blockSizeInBytes / texelInfo.pixelsPerBlock);
    m_texelSize = texelSize;

    auto baseShapeInfo = _getBaseShapeInfo(desc.type);
    m_baseShape = baseShapeInfo;
    if (!baseShapeInfo)
        return SLANG_FAIL;

    auto formatInfo = _getFormatInfo(desc.format);
    m_formatInfo = formatInfo;
    if (!formatInfo)
        return SLANG_FAIL;

    int32_t rank = baseShapeInfo->rank;
    int32_t effectiveArrayElementCount = desc.arrayLength * baseShapeInfo->implicitArrayElementCount;
    m_effectiveArrayElementCount = effectiveArrayElementCount;

    int32_t extents[kMaxRank];
    extents[0] = desc.size.width;
    extents[1] = desc.size.height;
    extents[2] = desc.size.depth;

    for (int32_t axis = rank; axis < kMaxRank; ++axis)
        extents[axis] = 1;

    int32_t levelCount = desc.mipCount;

    m_mipLevels.resize(levelCount);

    int64_t totalDataSize = 0;
    for (int32_t levelIndex = 0; levelIndex < levelCount; ++levelIndex)
    {
        auto& level = m_mipLevels[levelIndex];

        for (int32_t axis = 0; axis < kMaxRank; ++axis)
        {
            int32_t extent = extents[axis] >> levelIndex;
            if (extent < 1)
                extent = 1;
            level.extents[axis] = extent;
        }

        level.pitches[0] = texelSize;
        for (int32_t axis = 1; axis < kMaxRank + 1; ++axis)
        {
            level.pitches[axis] = level.pitches[axis - 1] * level.extents[axis - 1];
        }

        int64_t levelDataSize = texelSize;
        levelDataSize *= effectiveArrayElementCount;
        for (int32_t axis = 0; axis < rank; ++axis)
            levelDataSize *= int64_t(level.extents[axis]);

        level.offset = totalDataSize;
        totalDataSize += levelDataSize;
    }

    void* textureData = malloc((size_t)totalDataSize);
    m_data = textureData;

    if (initData)
    {
        int32_t subresourceCounter = 0;
        for (int32_t arrayElementIndex = 0; arrayElementIndex < effectiveArrayElementCount; ++arrayElementIndex)
        {
            for (int32_t mip = 0; mip < m_desc.mipCount; ++mip)
            {
                int32_t subresourceIndex = subresourceCounter++;

                auto dstRowPitch = m_mipLevels[mip].pitches[1];
                auto dstLayerPitch = m_mipLevels[mip].pitches[2];
                auto dstArrayPitch = m_mipLevels[mip].pitches[3];

                auto textureRowSize = m_mipLevels[mip].extents[0] * texelSize;

                auto rowCount = m_mipLevels[mip].extents[1];
                auto depthLayerCount = m_mipLevels[mip].extents[2];

                auto& srcImage = initData[subresourceIndex];
                ptrdiff_t srcRowPitch = ptrdiff_t(srcImage.rowPitch);
                ptrdiff_t srcLayerPitch = ptrdiff_t(srcImage.slicePitch);

                char* dstLevel = (char*)textureData + m_mipLevels[mip].offset;
                char* dstImage = dstLevel + dstArrayPitch * arrayElementIndex;

                const char* srcLayer = (const char*)srcImage.data;
                char* dstLayer = dstImage;

                for (int32_t depthLayer = 0; depthLayer < depthLayerCount; ++depthLayer)
                {
                    const char* srcRow = srcLayer;
                    char* dstRow = dstLayer;

                    for (int32_t row = 0; row < rowCount; ++row)
                    {
                        memcpy(dstRow, srcRow, textureRowSize);

                        srcRow += srcRowPitch;
                        dstRow += dstRowPitch;
                    }

                    srcLayer += srcLayerPitch;
                    dstLayer += dstLayerPitch;
                }
            }
        }
    }

    return SLANG_OK;
}

TextureViewImpl::TextureViewImpl(TextureImpl* texture, const TextureViewDesc& desc)
    : TextureView(texture, desc)
    , m_texture(texture)
{
}

slang_prelude::TextureDimensions TextureViewImpl::GetDimensions(int mip)
{
    slang_prelude::TextureDimensions dimensions = {};

    TextureImpl* texture = m_texture;
    auto& desc = texture->m_desc;
    auto baseShape = texture->m_baseShape;

    dimensions.arrayElementCount = desc.arrayLength;
    dimensions.numberOfLevels = desc.mipCount;
    dimensions.shape = baseShape->rank;
    dimensions.width = desc.size.width;
    dimensions.height = desc.size.height;
    dimensions.depth = desc.size.depth;

    return dimensions;
}

void TextureViewImpl::Load(const int32_t* texelCoords, void* outData, size_t dataSize)
{
    void* texelPtr = _getTexelPtr(texelCoords);

    m_texture->m_formatInfo->unpackFunc(texelPtr, outData, dataSize);
}

void TextureViewImpl::Sample(
    slang_prelude::SamplerState samplerState,
    const float* coords,
    void* outData,
    size_t dataSize
)
{
    // We have no access to information from fragment quads, so we cannot
    // compute the finite-difference derivatives needed from `coords`.
    //
    // The only reasonable thing to do is to sample mip level zero.
    //
    SampleLevel(samplerState, coords, 0.0f, outData, dataSize);
}

void TextureViewImpl::SampleLevel(
    slang_prelude::SamplerState samplerState,
    const float* coords,
    float level,
    void* outData,
    size_t dataSize
)
{
    TextureImpl* texture = m_texture;
    auto baseShape = texture->m_baseShape;
    auto& desc = texture->m_desc;
    int32_t rank = baseShape->rank;
    int32_t baseCoordCount = baseShape->baseCoordCount;

    int32_t integerMipLevel = int32_t(level + 0.5f);
    if (integerMipLevel >= desc.mipCount)
        integerMipLevel = desc.mipCount - 1;
    if (integerMipLevel < 0)
        integerMipLevel = 0;

    auto& mipLevelInfo = texture->m_mipLevels[integerMipLevel];

    // The coordinates after the base coordinates select an array element: an explicit array index
    // for the *Array types, or a cube face (the implicit array the base shape table describes).
    // The order matches slang's CPU prelude (slang-cpp-types.h in the slang toolchain), which
    // forwards TextureCubeArray's float4 (u, v, face, cubeIndex) location component-wise; the
    // end-to-end test cpu-texture-cube-array-shader-sampling exercises that packing. A cube
    // array consumes both extra coordinates, in this order: the face, then the cube index.
    // isCubeArray implies hasElementCoordinate, since TextureCubeArray is one of the *Array
    // types the helper recognizes, so the face was consumed by the preceding branch.
    bool isCubeArray = desc.type == TextureType::TextureCubeArray;
    bool hasElementCoordinate = _isTextureArrayType(desc.type) || baseShape->implicitArrayElementCount > 1;
    int32_t effectiveArrayElementCount = texture->m_effectiveArrayElementCount;
    int32_t coordIndex = baseCoordCount;
    int32_t elementIndex = 0;
    if (hasElementCoordinate)
        elementIndex = int32_t(coords[coordIndex++] + 0.5f);
    if (isCubeArray)
    {
        int32_t cubeIndex = int32_t(coords[coordIndex++] + 0.5f);
        elementIndex = _combineCubeArrayElementIndex(elementIndex, cubeIndex, baseShape, desc.arrayLength);
    }
    if (elementIndex >= effectiveArrayElementCount)
        elementIndex = effectiveArrayElementCount - 1;
    if (elementIndex < 0)
        elementIndex = 0;

    // Note: for now we are just going to do nearest-neighbor sampling
    //
    int64_t texelOffset = mipLevelInfo.offset;
    texelOffset += elementIndex * mipLevelInfo.pitches[3];
    for (int32_t axis = 0; axis < rank; ++axis)
    {
        int32_t extent = mipLevelInfo.extents[axis];

        float coord = coords[axis];

        // TODO: deal with wrap/clamp/repeat if `coord < 0` or `coord > 1`

        int32_t integerCoord = int32_t(coord * (extent - 1) + 0.5f);

        if (integerCoord >= extent)
            integerCoord = extent - 1;
        if (integerCoord < 0)
            integerCoord = 0;

        texelOffset += integerCoord * mipLevelInfo.pitches[axis];
    }

    auto texelPtr = (const char*)texture->m_data + texelOffset;

    m_texture->m_formatInfo->unpackFunc(texelPtr, outData, dataSize);
}

void* TextureViewImpl::refAt(const uint32_t* texelCoords)
{
    return _getTexelPtr((const int32_t*)texelCoords);
}

void* TextureViewImpl::_getTexelPtr(const int32_t* texelCoords)
{
    TextureImpl* texture = m_texture;
    auto baseShape = texture->m_baseShape;
    auto& desc = texture->m_desc;

    int32_t rank = baseShape->rank;
    int32_t baseCoordCount = baseShape->baseCoordCount;

    // Mirrors SampleLevel: the coordinates after the base coordinates select an array element
    // (an explicit array index for the *Array types, or a cube face from the implicit array),
    // and a cube array consumes both, in this order: the face, then the cube index, combined by
    // _combineCubeArrayElementIndex. isCubeArray implies hasElementCoordinate, since
    // TextureCubeArray is one of the *Array types the helper recognizes.
    bool isCubeArray = desc.type == TextureType::TextureCubeArray;
    bool hasElementCoordinate = _isTextureArrayType(desc.type) || baseShape->implicitArrayElementCount > 1;
    bool isMultisample = desc.sampleCount > 1;
    bool hasMipLevels = !isMultisample;

    int32_t effectiveArrayElementCount = texture->m_effectiveArrayElementCount;

    int32_t coordIndex = baseCoordCount;
    int32_t elementIndex = 0;
    if (hasElementCoordinate)
        elementIndex = texelCoords[coordIndex++];
    if (isCubeArray)
    {
        elementIndex =
            _combineCubeArrayElementIndex(elementIndex, texelCoords[coordIndex++], baseShape, desc.arrayLength);
    }
    if (elementIndex >= effectiveArrayElementCount)
        elementIndex = effectiveArrayElementCount - 1;
    if (elementIndex < 0)
        elementIndex = 0;

    // For a multisample shape the next coordinate is the per-texel sample slot, after the
    // element index. The CPU backend stores a single value per texel, so the slot is consumed
    // and ignored.
    int32_t mip = 0;
    if (!hasMipLevels)
        mip = texelCoords[coordIndex++];
    if (mip >= desc.mipCount)
        mip = desc.mipCount - 1;
    if (mip < 0)
        mip = 0;

    auto& mipLevelInfo = texture->m_mipLevels[mip];

    int64_t texelOffset = mipLevelInfo.offset;
    texelOffset += elementIndex * mipLevelInfo.pitches[3];
    for (int32_t axis = 0; axis < rank; ++axis)
    {
        int32_t coord = texelCoords[axis];
        if (coord >= mipLevelInfo.extents[axis])
            coord = mipLevelInfo.extents[axis] - 1;
        if (coord < 0)
            coord = 0;

        texelOffset += coord * mipLevelInfo.pitches[axis];
    }

    return (uint8_t*)texture->m_data + texelOffset;
}

Result DeviceImpl::createTexture(const TextureDesc& desc_, const SubresourceData* initData, ITexture** outTexture)
{
    if (desc_.heap)
        return SLANG_E_NOT_AVAILABLE;

    TextureDesc desc = fixupTextureDesc(desc_);
    RefPtr<TextureImpl> texture = new TextureImpl(this, desc);
    SLANG_RETURN_ON_FAIL(texture->init(initData));
    returnComPtr(outTexture, texture);
    return SLANG_OK;
}

Result DeviceImpl::createTextureView(ITexture* texture, const TextureViewDesc& desc, ITextureView** outView)
{
    RefPtr<TextureViewImpl> view = new TextureViewImpl(checked_cast<TextureImpl*>(texture), desc);
    if (view->m_desc.format == Format::Undefined)
        view->m_desc.format = view->m_texture->m_desc.format;
    view->m_desc.subresourceRange = view->m_texture->resolveSubresourceRange(desc.subresourceRange);
    returnComPtr(outView, view);
    return SLANG_OK;
}

Result DeviceImpl::readTexture(
    ITexture* texture,
    uint32_t layer,
    uint32_t mip,
    const SubresourceLayout& layout,
    void* outData
)
{
    auto textureImpl = checked_cast<TextureImpl*>(texture);

    // Get src + dest buffers.
    uint8_t* srcBuffer = (uint8_t*)textureImpl->m_data;
    uint8_t* dstBuffer = (uint8_t*)outData;

    // Should be able to make assumption that subresource layout info
    // matches those stored in the mip. If they don't match, this is a bug.
    TextureImpl::MipLevel mipLevelInfo = textureImpl->m_mipLevels[mip];
    SLANG_RHI_ASSERT(mipLevelInfo.extents[0] == layout.size.width);
    SLANG_RHI_ASSERT(mipLevelInfo.extents[1] == layout.size.height);
    SLANG_RHI_ASSERT(mipLevelInfo.extents[2] == layout.size.depth);
    SLANG_RHI_ASSERT(mipLevelInfo.pitches[1] == layout.rowPitch);
    SLANG_RHI_ASSERT(mipLevelInfo.pitches[2] == layout.slicePitch);

    // Step forward to the mip data in the texture.
    srcBuffer += mipLevelInfo.offset;

    // Copy a row at a time.
    for (int z = 0; z < layout.size.depth; z++)
    {
        uint8_t* srcRow = srcBuffer;
        uint8_t* dstRow = dstBuffer;
        for (int y = 0; y < layout.rowCount; y++)
        {
            std::memcpy(dstRow, srcRow, layout.rowPitch);
            srcRow += layout.rowPitch;
            dstRow += layout.rowPitch;
        }
        srcBuffer += layout.slicePitch;
        dstBuffer += layout.slicePitch;
    }

    return SLANG_OK;
}

} // namespace rhi::cpu
