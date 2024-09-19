#include "cpu-texture-view.h"

namespace rhi::cpu {

slang_prelude::TextureDimensions TextureViewImpl::GetDimensions(int mipLevel)
{
    slang_prelude::TextureDimensions dimensions = {};

    TextureImpl* texture = m_texture;
    auto& desc = texture->_getDesc();
    auto baseShape = texture->m_baseShape;

    dimensions.arrayElementCount = desc.arrayLength;
    dimensions.numberOfLevels = desc.numMipLevels;
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
    if (integerMipLevel >= desc.numMipLevels)
        integerMipLevel = desc.numMipLevels - 1;
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
    texelOffset += elementIndex * mipLevelInfo.strides[3];
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

        texelOffset += integerCoord * mipLevelInfo.strides[axis];
    }

    auto texelPtr = (char const*)texture->m_data + texelOffset;

    m_texture->m_formatInfo->unpackFunc(texelPtr, outData, dataSize);
}

void* TextureViewImpl::refAt(const uint32_t* texelCoords)
{
    return _getTexelPtr((int32_t const*)texelCoords);
}

void* TextureViewImpl::_getTexelPtr(int32_t const* texelCoords)
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

    int32_t mipLevel = 0;
    if (!hasMipLevels)
        mipLevel = texelCoords[coordIndex++];
    if (mipLevel >= desc.numMipLevels)
        mipLevel = desc.numMipLevels - 1;
    if (mipLevel < 0)
        mipLevel = 0;

    auto& mipLevelInfo = texture->m_mipLevels[mipLevel];

    int64_t texelOffset = mipLevelInfo.offset;
    texelOffset += elementIndex * mipLevelInfo.strides[3];
    for (int32_t axis = 0; axis < rank; ++axis)
    {
        int32_t coord = texelCoords[axis];
        if (coord >= mipLevelInfo.extents[axis])
            coord = mipLevelInfo.extents[axis] - 1;
        if (coord < 0)
            coord = 0;

        texelOffset += texelCoords[axis] * mipLevelInfo.strides[axis];
    }

    return (uint8_t*)texture->m_data + texelOffset;
}

} // namespace rhi::cpu
