#include "cpu-texture.h"
#include "cpu-device.h"

namespace rhi::cpu {

inline const CPUTextureBaseShapeInfo* _getBaseShapeInfo(TextureType baseShape)
{
    return &kCPUTextureBaseShapeInfos[(int)baseShape];
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

TextureImpl::TextureImpl(Device* device, const TextureDesc& desc)
    : Texture(device, desc)
{
}

TextureImpl::~TextureImpl()
{
    m_defaultView.setNull();
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

Result TextureImpl::getDefaultView(ITextureView** outTextureView)
{
    if (!m_defaultView)
    {
        SLANG_RETURN_ON_FAIL(m_device->createTextureView(this, {}, (ITextureView**)m_defaultView.writeRef()));
        m_defaultView->setInternalReferenceCount(1);
    }
    returnComPtr(outTextureView, m_defaultView);
    return SLANG_OK;
}

TextureViewImpl::TextureViewImpl(Device* device, const TextureViewDesc& desc)
    : TextureView(device, desc)
{
}

slang_prelude::TextureDimensions TextureViewImpl::GetDimensions(int mip)
{
    slang_prelude::TextureDimensions dimensions = {};

    TextureImpl* texture = m_texture;
    auto& desc = texture->_getDesc();
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
    auto& desc = texture->_getDesc();
    int32_t rank = baseShape->rank;
    int32_t baseCoordCount = baseShape->baseCoordCount;

    int32_t integerMipLevel = int32_t(level + 0.5f);
    if (integerMipLevel >= desc.mipCount)
        integerMipLevel = desc.mipCount - 1;
    if (integerMipLevel < 0)
        integerMipLevel = 0;

    auto& mipLevelInfo = texture->m_mipLevels[integerMipLevel];

    bool isArray = (desc.arrayLength > 1) || (desc.type == rhi::TextureType::TextureCube);
    int32_t effectiveArrayElementCount = texture->m_effectiveArrayElementCount;
    int32_t coordIndex = baseCoordCount;
    int32_t elementIndex = 0;
    if (isArray)
        elementIndex = int32_t(coords[coordIndex++] + 0.5f);
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
    auto& desc = texture->_getDesc();

    int32_t rank = baseShape->rank;
    int32_t baseCoordCount = baseShape->baseCoordCount;

    bool isArray = (desc.arrayLength > 1) || (desc.type == rhi::TextureType::TextureCube);
    bool isMultisample = desc.sampleCount > 1;
    bool hasMipLevels = !isMultisample;

    int32_t effectiveArrayElementCount = texture->m_effectiveArrayElementCount;

    int32_t coordIndex = baseCoordCount;
    int32_t elementIndex = 0;
    if (isArray)
        elementIndex = texelCoords[coordIndex++];
    if (elementIndex >= effectiveArrayElementCount)
        elementIndex = effectiveArrayElementCount - 1;
    if (elementIndex < 0)
        elementIndex = 0;

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

        texelOffset += texelCoords[axis] * mipLevelInfo.pitches[axis];
    }

    return (uint8_t*)texture->m_data + texelOffset;
}

Result DeviceImpl::createTexture(const TextureDesc& desc_, const SubresourceData* initData, ITexture** outTexture)
{
    TextureDesc desc = fixupTextureDesc(desc_);
    RefPtr<TextureImpl> texture = new TextureImpl(this, desc);
    SLANG_RETURN_ON_FAIL(texture->init(initData));
    returnComPtr(outTexture, texture);
    return SLANG_OK;
}

Result DeviceImpl::createTextureView(ITexture* texture, const TextureViewDesc& desc, ITextureView** outView)
{
    RefPtr<TextureViewImpl> view = new TextureViewImpl(this, desc);
    view->m_texture = checked_cast<TextureImpl*>(texture);
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
