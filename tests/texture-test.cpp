#include "texture-test.h"
#include "core/common.h"
#include "rhi-shared.h"
#include "format-conversion.h"
#include <cmath>
#include <random>

/// If set to 1, the default format list to test will be all formats
/// other than Format::Undefined.
#define SLANG_RHI_TEST_ALL_FORMATS 0

/// If set to 1, then default behavior is for textures that support
/// non-power-of-2 sizes to test them unless explicitly disabled
/// by the test.
#define SLANG_RHI_TEST_ALL_SIZES_BY_DEFAULT 0

namespace rhi::testing {

//----------------------------------------------------------
// Helpers
//----------------------------------------------------------

bool isValidDescriptor(IDevice* device, const TextureDesc& desc)
{
    // WGPU does not support mip levels for 1D textures.
    if (device->getDeviceType() == DeviceType::WGPU && desc.type == TextureType::Texture1D && desc.mipLevelCount != 1)
        return false;
    // WGPU does not support 1D texture arrays.
    if (device->getDeviceType() == DeviceType::WGPU && desc.type == TextureType::Texture1DArray)
        return false;
    // Metal does not support mip levels for 1D textures (and 1d texture arrays).
    if (device->getDeviceType() == DeviceType::Metal &&
        (desc.type == TextureType::Texture1D || desc.type == TextureType::Texture1DArray) && desc.mipLevelCount != 1)
        return false;
    // Metal does not support multisampled textures with 1 sample
    if (device->getDeviceType() == DeviceType::Metal && isMultisamplingType(desc.type) && desc.sampleCount == 1)
        return false;
    // CUDA does not support multisample textures.
    if (device->getDeviceType() == DeviceType::CUDA && isMultisamplingType(desc.type))
        return false;
    // Mip mapped multisampled textures not supported
    if (isMultisamplingType(desc.type) && desc.mipLevelCount > 1)
        return false;
    // Array multisampled textures not supported on WebGPU
    if (device->getDeviceType() == DeviceType::WGPU && isMultisamplingType(desc.type) && desc.getLayerCount() > 1)
        return false;
    // Anything with more than 1 layer won't work properly with CPU textures
    if (device->getDeviceType() == DeviceType::CPU && desc.getLayerCount() > 1)
        return false;
    return true;
}

bool getArrayType(TextureType type, TextureType& outArrayType)
{
    switch (type)
    {
    case TextureType::Texture1D:
    case TextureType::Texture1DArray:
        outArrayType = TextureType::Texture1DArray;
        return true;
    case TextureType::Texture2D:
    case TextureType::Texture2DArray:
        outArrayType = TextureType::Texture2DArray;
        return true;
    case TextureType::Texture2DMS:
    case TextureType::Texture2DMSArray:
        outArrayType = TextureType::Texture2DMSArray;
        return true;
    case TextureType::TextureCube:
    case TextureType::TextureCubeArray:
        outArrayType = TextureType::TextureCubeArray;
        return true;
    default:
        return false;
    }
}

bool getScalarType(TextureType type, TextureType& outScalarType)
{
    switch (type)
    {
    case TextureType::Texture1DArray:
        outScalarType = TextureType::Texture1D;
        return true;
    case TextureType::Texture2DArray:
        outScalarType = TextureType::Texture2D;
        return true;
    case TextureType::Texture2DMSArray:
        outScalarType = TextureType::Texture2DMS;
        return true;
    case TextureType::TextureCubeArray:
        outScalarType = TextureType::TextureCube;
        return true;
    default:
        outScalarType = type;
        return true;
    }
}

bool getMultisampleType(TextureType type, TextureType& outArrayType)
{
    switch (type)
    {
    case TextureType::Texture2D:
    case TextureType::Texture2DMS:
        outArrayType = TextureType::Texture2DMS;
        return true;
    case TextureType::Texture2DArray:
    case TextureType::Texture2DMSArray:
        outArrayType = TextureType::Texture2DMSArray;
        return true;
    default:
        return false;
    }
}

//----------------------------------------------------------
// TextureData
//----------------------------------------------------------

void TextureData::init(IDevice* device_, const TextureDesc& desc_, TextureInitMode initMode_, int initSeed_)
{
    device = device_;
    desc = fixupTextureDesc(desc_);
    formatInfo = getFormatInfo(desc.format);
    REQUIRE_CALL(device->getFormatSupport(desc.format, &formatSupport));

    desc.memoryType = MemoryType::DeviceLocal;

    REQUIRE(is_set(formatSupport, FormatSupport::Texture));

    desc.usage |= TextureUsage::CopySource | TextureUsage::CopyDestination;

    // D3D12 needs multisampled textures to be render targets.
    if (isMultisamplingType(desc_.type))
        desc.usage |= TextureUsage::RenderTarget;

    // Only add shader resource usage if format supports loading.
    if (is_set(formatSupport, FormatSupport::ShaderLoad))
        desc.usage |= TextureUsage::ShaderResource;

    // Initializing multi-aspect textures is not supported
    if (formatInfo.hasDepth && formatInfo.hasStencil)
        initMode_ = TextureInitMode::None;

    // Initialize subresources
    initData(initMode_, initSeed_);
}

void TextureData::initData(TextureInitMode initMode_, int initSeed_)
{
    initMode = initMode_;
    initSeed = initSeed_;

    subresources.clear();
    subresourceData.clear();

    for (uint32_t layer = 0; layer < desc.getLayerCount(); ++layer)
    {
        for (uint32_t mipLevel = 0; mipLevel < desc.mipLevelCount; ++mipLevel)
        {
            SubresourceLayout layout;
            calcSubresourceRegionLayout(desc, mipLevel, {0, 0, 0}, Extents::kWholeTexture, 1, &layout);

            Subresource sr;
            sr.layer = layer;
            sr.mipLevel = mipLevel;
            sr.layout = layout;
            sr.data = std::unique_ptr<uint8_t[]>(new uint8_t[sr.layout.sizeInBytes]);

            sr.subresourceData.data = sr.data.get();
            sr.subresourceData.strideY = sr.layout.strideY;
            sr.subresourceData.strideZ = sr.layout.strideZ;

            switch (initMode)
            {
            case rhi::testing::TextureInitMode::None:
                break;
            case rhi::testing::TextureInitMode::Zeros:
                memset(sr.data.get(), 0, sr.layout.sizeInBytes);
                break;
            case rhi::testing::TextureInitMode::Invalid:
                memset(sr.data.get(), 0xcd, sr.layout.sizeInBytes);
                break;
            case rhi::testing::TextureInitMode::MipLevel:
                memset(sr.data.get(), mipLevel, sr.layout.sizeInBytes);
                break;
            case rhi::testing::TextureInitMode::Random:
                std::mt19937 rng(initSeed + layer * desc.mipLevelCount + mipLevel);
                std::uniform_int_distribution<int> dist(0, 255);
                for (size_t i = 0; i < sr.layout.sizeInBytes; ++i)
                    sr.data[i] = (uint8_t)dist(rng);
            }

            subresourceData.push_back(sr.subresourceData);
            subresources.push_back(std::move(sr));
        }
    }
}

Result TextureData::createTexture(ITexture** texture) const
{
    const SubresourceData* sd = initMode == TextureInitMode::None ? nullptr : subresourceData.data();
    return device->createTexture(desc, sd, texture);
}

void TextureData::checkEqual(
    Offset3D thisOffset,
    ITexture* texture,
    Offset3D textureOffset,
    Extents textureExtents,
    bool compareOutsideRegion
) const
{
    const TextureDesc& otherDesc = texture->getDesc();
    CHECK_EQ(otherDesc.type, desc.type);
    CHECK_EQ(otherDesc.arrayLength, desc.arrayLength);

    for (uint32_t layer = 0; layer < desc.getLayerCount(); ++layer)
    {
        checkLayersEqual(layer, thisOffset, texture, layer, textureOffset, textureExtents, compareOutsideRegion);
    }
}

void TextureData::checkLayersEqual(
    int thisLayer,
    Offset3D thisOffset,
    ITexture* texture,
    int textureLayer,
    Offset3D textureOffset,
    Extents textureExtents,
    bool compareOutsideRegion
) const
{
    const TextureDesc& otherDesc = texture->getDesc();
    CHECK_EQ(otherDesc.mipLevelCount, desc.mipLevelCount);

    for (uint32_t mipLevel = 0; mipLevel < desc.mipLevelCount; ++mipLevel)
    {
        checkMipLevelsEqual(
            thisLayer,
            mipLevel,
            thisOffset,
            texture,
            textureLayer,
            mipLevel,
            textureOffset,
            textureExtents,
            compareOutsideRegion
        );
    }
}

void checkRegionsEqual(
    const void* dataA_,
    const SubresourceLayout& layoutA,
    Offset3D offsetA,
    const void* dataB_,
    const SubresourceLayout& layoutB,
    Offset3D offsetB,
    Extents extents
)
{
    /*
    fprintf(
        stderr,
        "    Oa: [%d,%d,%d], Ob: [%d,%d,%d], Ex: [%d,%d,%d]\n",
        offsetA.x,
        offsetA.y,
        offsetA.z,
        offsetB.x,
        offsetB.y,
        offsetB.z,
        extents.width,
        extents.height,
        extents.depth
    );
    */

    const uint8_t* dataA = (const uint8_t*)dataA_;
    const uint8_t* dataB = (const uint8_t*)dataB_;

    // Can't compare regions with different block sizes.
    CHECK_EQ(layoutA.blockWidth, layoutB.blockWidth);
    CHECK_EQ(layoutA.blockHeight, layoutB.blockHeight);
    CHECK_EQ(layoutA.strideX, layoutB.strideX);

    // Check region is valid for A
    CHECK_GE(layoutA.size.width, offsetA.x + extents.width);
    CHECK_GE(layoutA.size.height, offsetA.y + extents.height);
    CHECK_GE(layoutA.size.depth, offsetA.z + extents.depth);

    // Check region is valid for B
    CHECK_GE(layoutB.size.width, offsetB.x + extents.width);
    CHECK_GE(layoutB.size.height, offsetB.y + extents.height);
    CHECK_GE(layoutB.size.depth, offsetB.z + extents.depth);

    // Calculate overall dimensions in blocks rather than pixels to handle compressed textures.
    uint32_t sliceOffsetA = offsetA.z;
    uint32_t rowOffsetA = math::divideRoundedUp(offsetA.y, layoutA.blockHeight);
    uint32_t colOffsetA = math::divideRoundedUp(offsetA.x, layoutA.blockWidth);
    uint32_t sliceOffsetB = offsetB.z;
    uint32_t rowOffsetB = math::divideRoundedUp(offsetB.y, layoutB.blockHeight);
    uint32_t colOffsetB = math::divideRoundedUp(offsetB.x, layoutB.blockWidth);
    uint32_t sliceCount = extents.depth;
    uint32_t rowCount = math::divideRoundedUp(extents.height, layoutA.blockHeight);
    uint32_t colCount = math::divideRoundedUp(extents.width, layoutA.blockWidth);

    // Iterate over whole texture, checking each block.
    for (uint32_t slice = 0; slice < sliceCount; slice++)
    {
        const uint8_t* sliceA = dataA + (slice + sliceOffsetA) * layoutA.strideZ;
        const uint8_t* sliceB = dataB + (slice + sliceOffsetB) * layoutB.strideZ;

        // Iterate rows
        for (uint32_t row = 0; row < rowCount; row++)
        {
            const uint8_t* rowA = sliceA + (row + rowOffsetA) * layoutA.strideY;
            const uint8_t* rowB = sliceB + (row + rowOffsetB) * layoutB.strideY;

            // Iterate columns.
            for (uint32_t col = 0; col < colCount; col++)
            {
                const uint8_t* blockA = rowA + (col + colOffsetA) * layoutA.strideX;
                const uint8_t* blockB = rowB + (col + colOffsetB) * layoutB.strideX;
                bool areequal = memcmp(blockA, blockB, layoutA.strideX) == 0;
                CHECK(areequal);
                if (!areequal)
                    return;
            }
        }
    }
}

void TextureData::checkMipLevelsEqual(
    int thisLayer,
    int thisMipLevel,
    Offset3D thisOffset,
    ITexture* texture,
    int textureLayer,
    int textureMipLevel,
    Offset3D textureOffset,
    Extents textureExtents,
    bool compareOutsideRegion
) const
{
    Texture* textureImpl = checked_cast<Texture*>(texture);

    const TextureDesc& otherDesc = textureImpl->getDesc();

    CHECK_EQ(otherDesc.format, desc.format);

    const Subresource& thisSubresource = getSubresource(thisLayer, thisMipLevel);
    const SubresourceLayout& thisLayout = thisSubresource.layout;

    SubresourceLayout textureLayout;
    REQUIRE_CALL(textureImpl->getSubresourceLayout(textureMipLevel, &textureLayout));

    ComPtr<ISlangBlob> blob;
    Size rowPitch;
    REQUIRE_CALL(
        textureImpl->getDevice()->readTexture(textureImpl, textureLayer, textureMipLevel, blob.writeRef(), &rowPitch)
    );

    // For compressed textures, raise error if attempting to check non-aligned blocks
    if (formatInfo.blockWidth > 1)
    {
        CHECK_EQ(textureOffset.x % formatInfo.blockWidth, 0);
        if (textureExtents.width != Extents::kWholeTexture.width)
            CHECK_EQ(textureExtents.width % formatInfo.blockWidth, 0);
    }
    if (formatInfo.blockHeight > 1)
    {
        CHECK_EQ(textureOffset.y % formatInfo.blockHeight, 0);
        if (textureExtents.height != Extents::kWholeTexture.height)
            CHECK_EQ(textureExtents.height % formatInfo.blockHeight, 0);
    }

    // Adjust extents if 'whole texture' specified.
    if (textureExtents.width == Extents::kWholeTexture.width)
        textureExtents.width = max(textureLayout.size.width - textureOffset.x, 1);
    if (textureExtents.height == Extents::kWholeTexture.height)
        textureExtents.height = max(textureLayout.size.height - textureOffset.y, 1);
    if (textureExtents.depth == Extents::kWholeTexture.depth)
        textureExtents.depth = max(textureLayout.size.depth - textureOffset.z, 1);

    if (!compareOutsideRegion)
    {
        // fprintf(stderr, "  Compare internal region\n");

        // Simple case - comparing the internal regions of 2 textures.
        checkRegionsEqual(
            thisSubresource.data.get(),
            thisLayout,
            thisOffset,
            blob->getBufferPointer(),
            textureLayout,
            textureOffset,
            textureExtents
        );
    }
    else
    {
        // fprintf(stderr, "  Compare external region\n");

        // More complex case, comparing the whole of 2 textures with the
        // region excluded. For this case the offsets must match, and the
        // offset/extents refer to the region to exclude.
        CHECK_EQ(thisOffset, textureOffset);

        // For simplicity, the (potentially 3D) texture is divided into 3x3x3
        // regions, with the central region being the region to exclude.
        // The surrounding regions are then compared.
        int zSizes[] =
            {textureOffset.z, textureExtents.depth, textureLayout.size.depth - textureExtents.depth - textureOffset.z};
        int ySizes[] = {
            textureOffset.y,
            textureExtents.height,
            textureLayout.size.height - textureExtents.height - textureOffset.y
        };
        int xSizes[] =
            {textureOffset.x, textureExtents.width, textureLayout.size.width - textureExtents.width - textureOffset.x};

        int offsetZ = 0;
        for (uint32_t regionZ = 0; regionZ < 3; regionZ++)
        {
            int sizeZ = zSizes[regionZ];
            int offsetY = 0;
            for (uint32_t regionY = 0; regionY < 3; regionY++)
            {
                int sizeY = ySizes[regionY];
                int offsetX = 0;
                for (uint32_t regionX = 0; regionX < 3; regionX++)
                {
                    int sizeX = xSizes[regionX];
                    if (regionX != 1 || regionY != 1 || regionZ != 1)
                    {
                        checkRegionsEqual(
                            thisSubresource.data.get(),
                            thisLayout,
                            {offsetX, offsetY, offsetZ},
                            blob->getBufferPointer(),
                            textureLayout,
                            {offsetX, offsetY, offsetZ},
                            {sizeX, sizeY, sizeZ}
                        );
                    }
                    offsetX += sizeX;
                }
                offsetY += sizeY;
            }
            offsetZ += sizeZ;
        }
    }
}

void TextureData::checkSliceEqual(
    ITexture* texture,
    int thisLayer,
    int thisMipLevel,
    int thisSlice,
    int textureLayer,
    int textureMipLevel
) const
{
    Texture* textureImpl = checked_cast<Texture*>(texture);

    const TextureDesc& otherDesc = textureImpl->getDesc();

    CHECK_EQ(otherDesc.format, desc.format);
    CHECK_EQ(desc.type, TextureType::Texture3D);
    CHECK((otherDesc.type == TextureType::Texture2D || otherDesc.type == TextureType::Texture2DArray));
    CHECK_EQ(otherDesc.size.width, desc.size.width);
    CHECK_EQ(otherDesc.size.height, desc.size.height);

    const Subresource& thisSubresource = getSubresource(thisLayer, thisMipLevel);
    const SubresourceLayout& thisLayout = thisSubresource.layout;

    SubresourceLayout textureLayout;
    REQUIRE_CALL(textureImpl->getSubresourceLayout(textureMipLevel, &textureLayout));

    ComPtr<ISlangBlob> blob;
    Size rowPitch;
    REQUIRE_CALL(
        textureImpl->getDevice()->readTexture(textureImpl, textureLayer, textureMipLevel, blob.writeRef(), &rowPitch)
    );

    // Calculate overall dimensions in blocks rather than pixels to handle compressed textures.
    uint32_t rowCount = thisLayout.rowCount;
    uint32_t colCount = thisLayout.size.width / formatInfo.blockWidth;

    uint8_t* thisData = thisSubresource.data.get();
    uint8_t* textureData = (uint8_t*)blob->getBufferPointer();

    // Iterate rows
    for (uint32_t row = 0; row < rowCount; row++)
    {
        // Iterate columns.
        for (uint32_t col = 0; col < colCount; col++)
        {
            // Get pointer to block within the whole cpu data.
            uint8_t* thisBlock = thisData + thisSlice * thisLayout.strideZ + row * thisLayout.strideY +
                                 col * formatInfo.blockSizeInBytes;

            // Get pointer to block within the region of the texture we're scanning
            uint8_t* textureBlock = textureData + row * textureLayout.strideY + col * formatInfo.blockSizeInBytes;

            // Compare the block of texels that make up this row/column
            bool blocks_equal = memcmp(thisBlock, textureBlock, formatInfo.blockSizeInBytes) == 0;
            CHECK(blocks_equal);

            // Avoid reporting every non-matching block.
            if (!blocks_equal)
                return;
        }
    }
}

void TextureData::checkEqualFloat(ITexture* texture, float epsilon) const
{
    Texture* textureImpl = checked_cast<Texture*>(texture);

    const TextureDesc& otherDesc = textureImpl->getDesc();

    CHECK_EQ(otherDesc.type, desc.type);
    CHECK_EQ(otherDesc.format, desc.format);
    CHECK_EQ(otherDesc.size.width, desc.size.width);
    CHECK_EQ(otherDesc.size.height, desc.size.height);
    CHECK_EQ(otherDesc.size.depth, desc.size.depth);
    CHECK_EQ(otherDesc.arrayLength, desc.arrayLength);
    CHECK_EQ(otherDesc.mipLevelCount, desc.mipLevelCount);

    UnpackFloatFunc unpackFloatFunc = getFormatConversionFuncs(desc.format).unpackFloatFunc;
    SLANG_RHI_ASSERT(unpackFloatFunc);
    size_t pixelSize = formatInfo.blockSizeInBytes / formatInfo.pixelsPerBlock;

    for (uint32_t layer = 0; layer < desc.getLayerCount(); ++layer)
    {
        for (uint32_t mipLevel = 0; mipLevel < desc.mipLevelCount; ++mipLevel)
        {
            const Subresource& sr = getSubresource(layer, mipLevel);

            ComPtr<ISlangBlob> blob;
            Size rowPitch;
            REQUIRE_CALL(textureImpl->getDevice()->readTexture(textureImpl, layer, mipLevel, blob.writeRef(), &rowPitch)
            );

            const uint8_t* expectedSlice = sr.data.get();
            const uint8_t* actualSlice = (uint8_t*)blob->getBufferPointer();

            for (uint32_t slice = 0; slice < sr.layout.size.depth; slice++)
            {
                const uint8_t* expectedRow = expectedSlice;
                const uint8_t* actualRow = actualSlice;

                for (uint32_t row = 0; row < sr.layout.rowCount; row++)
                {
                    const uint8_t* expectedData = expectedRow;
                    const uint8_t* actualData = actualRow;
                    bool isEqual = true;

                    for (uint32_t x = 0; x < sr.layout.size.width; x++)
                    {
                        float expected[4];
                        float actual[4];
                        unpackFloatFunc(expectedData + x * pixelSize, expected);
                        unpackFloatFunc(actualData + x * pixelSize, actual);
                        for (uint32_t i = 0; i < formatInfo.channelCount; i++)
                        {
                            // Note: Doing a check for each pixel is slow, so we do it per row.
                            // CHECK_EQ(std::abs(expected[i] - actual[i]) <= epsilon, true);
                            isEqual &= std::abs(expected[i] - actual[i]) <= epsilon;
                        }
                    }

                    CHECK(isEqual);

                    expectedRow += sr.layout.strideY;
                    actualRow += rowPitch;
                }

                expectedSlice += sr.layout.strideZ;
                actualSlice += rowPitch * sr.layout.rowCount;
            }
        }
    }
}

void TextureData::clearFloat(const float clearValue[4]) const
{
    for (uint32_t layer = 0; layer < desc.getLayerCount(); ++layer)
    {
        for (uint32_t mipLevel = 0; mipLevel < desc.mipLevelCount; ++mipLevel)
        {
            clearFloat(layer, mipLevel, clearValue);
        }
    }
}

void TextureData::clearFloat(uint32_t layer, uint32_t mipLevel, const float clearValue[4]) const
{
    const Subresource& subresource = getSubresource(layer, mipLevel);
    FormatConversionFuncs funcs = getFormatConversionFuncs(desc.format);
    SLANG_RHI_ASSERT(funcs.packFloatFunc);
    size_t pixelSize = formatInfo.blockSizeInBytes / formatInfo.pixelsPerBlock;
    uint8_t pixelData[16];
    funcs.packFloatFunc(clearValue, pixelData);
    for (uint32_t depth = 0; depth < subresource.layout.size.depth; depth++)
    {
        for (uint32_t row = 0; row < subresource.layout.rowCount; row++)
        {
            uint8_t* rowStart =
                subresource.data.get() + depth * subresource.layout.strideZ + row * subresource.layout.strideY;
            for (uint32_t x = 0; x < subresource.layout.size.width; x++)
            {
                ::memcpy(rowStart + x * pixelSize, pixelData, pixelSize);
            }
        }
    }
}

void TextureData::clearUint(const uint32_t clearValue[4]) const
{
    for (uint32_t layer = 0; layer < desc.getLayerCount(); ++layer)
    {
        for (uint32_t mipLevel = 0; mipLevel < desc.mipLevelCount; ++mipLevel)
        {
            clearUint(layer, mipLevel, clearValue);
        }
    }
}

void TextureData::clearUint(uint32_t layer, uint32_t mipLevel, const uint32_t clearValue[4]) const
{
    const Subresource& subresource = getSubresource(layer, mipLevel);
    FormatConversionFuncs funcs = getFormatConversionFuncs(desc.format);
    SLANG_RHI_ASSERT(funcs.packIntFunc);
    size_t pixelSize = formatInfo.blockSizeInBytes / formatInfo.pixelsPerBlock;
    uint8_t pixelData[16];
    funcs.packIntFunc(clearValue, pixelData);
    for (uint32_t depth = 0; depth < subresource.layout.size.depth; depth++)
    {
        for (uint32_t row = 0; row < subresource.layout.rowCount; row++)
        {
            uint8_t* rowStart =
                subresource.data.get() + depth * subresource.layout.strideZ + row * subresource.layout.strideY;
            for (uint32_t x = 0; x < subresource.layout.size.width; x++)
            {
                ::memcpy(rowStart + x * pixelSize, pixelData, pixelSize);
            }
        }
    }
}

void TextureData::clearSint(const int32_t clearValue[4]) const
{
    uint32_t clearValueUint[4];
    truncateBySintFormat(desc.format, reinterpret_cast<const uint32_t*>(clearValue), clearValueUint);
    clearUint(clearValueUint);
}

void TextureData::clearSint(uint32_t layer, uint32_t mipLevel, const int32_t clearValue[4]) const
{
    uint32_t clearValueUint[4];
    truncateBySintFormat(desc.format, reinterpret_cast<const uint32_t*>(clearValue), clearValueUint);
    clearUint(layer, mipLevel, clearValueUint);
}


//----------------------------------------------------------
// TextureTestOptions
//----------------------------------------------------------
void TextureTestOptions::run(std::function<void(const TextureTestVariant&)> func)
{
    m_current_callback = func;

    for (int i = 0; i < m_generator_lists.size(); i++)
    {
        executeGeneratorList(i);
    }
}

void TextureTestOptions::executeGeneratorList(int listIdx)
{
    m_current_list_idx = listIdx;

    TextureTestVariant variant;

#if SLANG_RHI_TEST_ALL_SIZES_BY_DEFAULT
    variant.powerOf2 = TTPowerOf2::Both;
#endif

    variant.descriptors.resize(m_numTextures);
    for (auto& desc : variant.descriptors)
    {
        desc.desc.type = (TextureType)-1;
        desc.desc.arrayLength = 0;
        desc.desc.sampleCount = 0;
        desc.desc.mipLevelCount = 0;
        desc.initMode = TextureInitMode::Random;
    }

    next(0, variant);
}

void TextureTestOptions::next(int nextIndex, TextureTestVariant variant)
{
    GeneratorList& current_list = m_generator_lists[m_current_list_idx];
    if (nextIndex < current_list.size())
    {
        int idx = nextIndex++;
        current_list[idx](nextIndex, variant);
    }
    else
    {
        for (auto& testTexture : variant.descriptors)
            if (!isValidDescriptor(m_device, testTexture.desc))
                return;

        m_current_callback(variant);
    }
}

void TextureTestOptions::addGenerator(GeneratorFunc generator)
{
    m_generator_lists.back().push_back(generator);
}

void TextureTestOptions::processVariantArg(TextureDesc baseDesc)
{
    addGenerator(
        [this, baseDesc](int state, TextureTestVariant variant)
        {
            // Explicit descriptor must be first argument
            SLANG_RHI_ASSERT(state == 1);
            for (auto& testTexture : variant.descriptors)
                testTexture.desc = baseDesc;
            next(state, variant);
        }
    );
}

void TextureTestOptions::processVariantArg(TestTextureDesc baseDesc)
{
    addGenerator(
        [this, baseDesc](int state, TextureTestVariant variant)
        {
            // Explicit descriptor must be first argument
            SLANG_RHI_ASSERT(state == 1);
            for (auto& testTexture : variant.descriptors)
                testTexture = baseDesc;
            next(state, variant);
        }
    );
}

void TextureTestOptions::processVariantArg(TextureTestVariant baseDesc)
{
    addGenerator(
        [this, baseDesc](int state, TextureTestVariant variant)
        {
            // Explicit descriptor must be first argument
            SLANG_RHI_ASSERT(state == 1);
            next(state, baseDesc);
        }
    );
}

void TextureTestOptions::processVariantArg(TextureInitMode initMode)
{
    addGenerator(
        [this, initMode](int state, TextureTestVariant variant)
        {
            for (auto& testTexture : variant.descriptors)
                testTexture.initMode = initMode;
            next(state, variant);
        }
    );
}

void TextureTestOptions::processVariantArg(TTShape shape)
{
    addGenerator(
        [this, shape](int state, TextureTestVariant variant)
        {
            if (is_set(shape, TTShape::D1))
            {
                for (auto& testTexture : variant.descriptors)
                    testTexture.desc.type = TextureType::Texture1D;
                next(state, variant);
            }
            if (is_set(shape, TTShape::D2))
            {
                for (auto& testTexture : variant.descriptors)
                    testTexture.desc.type = TextureType::Texture2D;
                next(state, variant);
            }
            if (is_set(shape, TTShape::D3))
            {
                for (auto& testTexture : variant.descriptors)
                    testTexture.desc.type = TextureType::Texture3D;
                next(state, variant);
            }
            if (is_set(shape, TTShape::Cube))
            {
                for (auto& testTexture : variant.descriptors)
                    testTexture.desc.type = TextureType::TextureCube;
                next(state, variant);
            }
        }
    );
}

void TextureTestOptions::processVariantArg(TextureType type)
{
    addGenerator(
        [this, type](int state, TextureTestVariant variant)
        {
            for (auto& testTexture : variant.descriptors)
                testTexture.desc.type = type;
            next(state, variant);
        }
    );
}

void TextureTestOptions::processVariantArg(TexTypes& types)
{
    addGenerator(
        [this, types](int state, TextureTestVariant variant)
        {
            for (TextureType type : types.values)
            {
                for (auto& testTexture : variant.descriptors)
                    testTexture.desc.type = type;
                next(state, variant);
            }
        }
    );
}

void TextureTestOptions::processVariantArg(TTMip mip)
{
    addGenerator(
        [this, mip](int state, TextureTestVariant variant)
        {
            if (is_set(mip, TTMip::Off))
            {
                for (auto& testTexture : variant.descriptors)
                    testTexture.desc.mipLevelCount = 1;
                next(state, variant);
            }
            if (is_set(mip, TTMip::On))
            {
                for (auto& testTexture : variant.descriptors)
                    testTexture.desc.mipLevelCount = kAllMipLevels;
                next(state, variant);
            }
        }
    );
}

void TextureTestOptions::processVariantArg(TTArray array)
{
    addGenerator(
        [this, array](int state, TextureTestVariant variant)
        {
            if (is_set(array, TTArray::Off))
            {
                for (auto& testTexture : variant.descriptors)
                    testTexture.desc.arrayLength = 1;
                next(state, variant);
            }
            if (is_set(array, TTArray::On))
            {
                for (auto& testTexture : variant.descriptors)
                    testTexture.desc.arrayLength = 4;
                next(state, variant);
            }
        }
    );
}

void TextureTestOptions::processVariantArg(TTMS multisample)
{
    addGenerator(
        [this, multisample](int state, TextureTestVariant variant)
        {
            if (is_set(multisample, TTMS::Off))
            {
                for (auto& testTexture : variant.descriptors)
                    testTexture.desc.sampleCount = 1;
                next(state, variant);
            }
            if (is_set(multisample, TTMS::On))
            {
                for (auto& testTexture : variant.descriptors)
                    testTexture.desc.sampleCount = 4;
                next(state, variant);
            }
        }
    );
}

void TextureTestOptions::processVariantArg(Format format)
{
    addGenerator(
        [this, format](int state, TextureTestVariant variant)
        {
            for (auto& testTexture : variant.descriptors)
                testTexture.desc.format = format;
            next(state, variant);
        }
    );
}

void TextureTestOptions::processVariantArg(const std::vector<Format>& formats)
{
    addGenerator(
        [this, formats](int state, TextureTestVariant variant)
        {
            for (Format format : formats)
            {
                for (auto& testTexture : variant.descriptors)
                    testTexture.desc.format = format;
                next(state, variant);
            }
        }
    );
}

void TextureTestOptions::processVariantArg(TTFmtDepth format)
{
    addGenerator(
        [this, format](int state, TextureTestVariant variant)
        {
            variant.formatFilter.depth = format;
            next(state, variant);
        }
    );
}

void TextureTestOptions::processVariantArg(TTFmtStencil format)
{
    addGenerator(
        [this, format](int state, TextureTestVariant variant)
        {
            variant.formatFilter.stencil = format;
            next(state, variant);
        }
    );
}

void TextureTestOptions::processVariantArg(TTFmtCompressed format)
{
    addGenerator(
        [this, format](int state, TextureTestVariant variant)
        {
            variant.formatFilter.compression = format;
            next(state, variant);
        }
    );
}

void TextureTestOptions::processVariantArg(TTPowerOf2 powerOf2)
{
    addGenerator(
        [this, powerOf2](int state, TextureTestVariant variant)
        {
            variant.powerOf2 = powerOf2;
            next(state, variant);
        }
    );
}

void TextureTestOptions::processVariantArg(TextureUsage usage)
{
    addGenerator(
        [this, usage](int state, TextureTestVariant variant)
        {
            for (auto& testTexture : variant.descriptors)
                testTexture.desc.usage |= usage;
            next(state, variant);
        }
    );
}

// checks filter, where mask is a bitfield with bit 1=allow off, 2=allow on
template<typename T>
inline bool _checkFilter(bool value, T mask)
{
    int bits = (int)mask;
    bool allow = false;
    if (bits & 1)
        allow |= !value;
    if (bits & 2)
        allow |= value;
    return allow;
}

bool FormatFilter::filter(Format format) const
{
    const FormatInfo& info = getFormatInfo(format);

    if (!_checkFilter(info.isCompressed, compression))
        return false;
    if (!_checkFilter(info.hasDepth, depth))
        return false;
    if (!_checkFilter(info.hasStencil, stencil))
        return false;
    return true;
}


// Nice selection of formats to test
Format kStandardFormats[] = {
    Format::D16Unorm,
    Format::D32FloatS8Uint,
    Format::D32Float,
    Format::RGBA32Uint,
    Format::RGB32Uint,
    Format::RGBA32Float,
    Format::R32Float,
    Format::RGBA16Float,
    Format::RGBA16Uint,
    Format::RGBA8Uint,
    Format::RGBA8Unorm,
    Format::RGBA8UnormSrgb,
    Format::RGBA16Snorm,
    Format::RGBA8Snorm,
    Format::RGB10A2Unorm,
    Format::BC1Unorm,
    Format::BC1UnormSrgb,
    Format::R64Uint,
};

void TextureTestOptions::postProcessVariant(int state, TextureTestVariant variant)
{
    bool anyUndefinedFormats = false;
    for (auto& testTexture : variant.descriptors)
    {
        SLANG_RHI_ASSERT(testTexture.desc.type != (TextureType)-1); // types must be specified

        // Defaults for arrays, mips and multisample are all off
        TextureDesc& desc = testTexture.desc;
        if (desc.arrayLength == 0)
            desc.arrayLength = 1;
        if (desc.mipLevelCount == 0)
            desc.mipLevelCount = 1;
        if (desc.sampleCount == 0)
            desc.sampleCount = 1;

        // If user has explicitly made it an array, switch to array type.
        // Note: has no effect if type already explicitly an array.
        if (desc.arrayLength > 1)
        {
            TextureType arrayType;
            if (!getArrayType(desc.type, arrayType))
                return;
            desc.type = arrayType;
        }

        // If user has explicitly made it multisampled, switch to multisampled type.
        // Note: has no effect if type already explicitly multisampled.
        if (desc.sampleCount > 1)
        {
            TextureType multisampleType;
            if (!getMultisampleType(desc.type, multisampleType))
                return;
            desc.type = multisampleType;
        }


        // Set size based on type.
        switch (desc.type)
        {
        case rhi::TextureType::Texture1D:
        case rhi::TextureType::Texture1DArray:
            desc.size = {512, 1, 1};
            break;
        case rhi::TextureType::Texture2D:
        case rhi::TextureType::Texture2DArray:
        case rhi::TextureType::Texture2DMS:
        case rhi::TextureType::Texture2DMSArray:
            desc.size = {32, 16, 1};
            break;
        case rhi::TextureType::Texture3D:
            desc.size = {16, 16, 4};
            break;
        case rhi::TextureType::TextureCube:
        case rhi::TextureType::TextureCubeArray:
            desc.size = {16, 16, 1};
            break;
        default:
            break;
        }

        // Ensure array size greater than 1 for any of the array types.
        switch (desc.type)
        {
        case rhi::TextureType::Texture1DArray:
        case rhi::TextureType::Texture2DArray:
        case rhi::TextureType::Texture2DMSArray:
        case rhi::TextureType::TextureCubeArray:
            desc.arrayLength = max(desc.arrayLength, 4U);
            break;
        default:
            break;
        }

        // Ensure sample count greater than 1 for any MS types
        switch (desc.type)
        {
        case rhi::TextureType::Texture2DMS:
        case rhi::TextureType::Texture2DMSArray:
            desc.sampleCount = max(desc.sampleCount, 2U);
            break;
        default:
            break;
        }

        // Can't init multisampled textures
        if (isMultisamplingType(testTexture.desc.type))
            testTexture.initMode = TextureInitMode::None;

        anyUndefinedFormats |= testTexture.desc.format == Format::Undefined;
    }

    if (anyUndefinedFormats)
    {
        // If format not specified, add standard test formats.
        // With SLANG_RHI_TEST_ALL_FORMATS, all except Format::Undefined are checked.
#if SLANG_RHI_TEST_ALL_FORMATS == 0
        for (Format format : kStandardFormats)
#else
        for (Format format = Format(1); format < Format::_Count; format = Format(int(format) + 1))
#endif
        {
            TextureTestVariant formatVariant = variant;
            for (auto& testTexture : formatVariant.descriptors)
                if (testTexture.desc.format == Format::Undefined)
                    testTexture.desc.format = format;
            next(state, formatVariant);
        }
    }
    else
    {
        // Format already specified so just pass through
        next(state, variant);
    }
}

void TextureTestOptions::filterFormat(int state, TextureTestVariant variant)
{
    for (auto& testTexture : variant.descriptors)
    {
        Format format = testTexture.desc.format;

        // Apply format mask filter
        if (!variant.formatFilter.filter(format))
            return;

        // Skip if device doesn't support format.
        FormatSupport support;
        m_device->getFormatSupport(format, &support);
        if (!is_set(support, FormatSupport::Texture))
            return;

        const FormatInfo& info = getFormatInfo(format);

        // WebGPU doesn't support writing into depth textures.
        if (m_device->getDeviceType() == DeviceType::Metal && (info.hasDepth || info.hasStencil))
            return;

        // WebGPU doesn't support writing into depth textures.
        if (m_device->getDeviceType() == DeviceType::WGPU && (info.hasDepth || info.hasStencil))
            return;

        // Skip texture types that don't support compression options.
        if (info.isCompressed)
        {
            if (!supportsCompressedFormats(testTexture.desc))
                return;
        }

        // Skip texture types that don't support depth/stencil options/
        if (info.hasDepth || info.hasStencil)
        {
            if (!supportsDepthFormats(testTexture.desc))
                return;
        }

        // Skip formats that don't support texture multisampling options/
        if (isMultisamplingType(testTexture.desc.type))
        {
            if (!formatSupportsMultisampling(format))
                return;
        }
    }

    next(state, variant);
}

void TextureTestOptions::applyTextureSize(int state, TextureTestVariant variant)
{
    if (is_set(variant.powerOf2, TTPowerOf2::On))
    {
        // Textures are configured with power-of-2 sizes by default so just pass through.
        next(state, variant);
    }
    if (is_set(variant.powerOf2, TTPowerOf2::Off))
    {
        // Make adjustments for power of 2 sizes. We need to increment dimensions
        // that aren't only 1 pixel wide until they're a non-power of 2 multiple
        // of the block size.
        for (auto& testTexture : variant.descriptors)
        {
            const FormatInfo& info = getFormatInfo(testTexture.desc.format);
            if (!info.supportsNonPowerOf2)
                return;

            for (auto& testTexture : variant.descriptors)
            {
                if (testTexture.desc.size.width > 1)
                {
                    while (math::isPowerOf2(testTexture.desc.size.width))
                        testTexture.desc.size.width += info.blockWidth;
                }
                if (testTexture.desc.size.height > 1)
                {
                    while (math::isPowerOf2(testTexture.desc.size.height))
                        testTexture.desc.size.height += info.blockHeight;
                }
                if (testTexture.desc.size.depth > 1)
                {
                    while (math::isPowerOf2(testTexture.desc.size.depth))
                        testTexture.desc.size.depth++;
                }
            }
        }
        next(state, variant);
    }
}

//----------------------------------------------------------
// TextureTestContext
//----------------------------------------------------------

TextureTestContext::TextureTestContext(IDevice* device)
    : m_device(device)
{
}

Result TextureTestContext::addTexture(TextureData&& data)
{
    ComPtr<ITexture> texture;
    SLANG_RETURN_ON_FAIL(data.createTexture(texture.writeRef()));
    m_textures.push_back(texture);
    m_datas.push_back(std::move(data));
    return SLANG_OK;
}

} // namespace rhi::testing
