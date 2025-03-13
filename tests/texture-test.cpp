#include "texture-test.h"
#include <cmath>
#include "core/common.h"
#include "rhi-shared.h"
#include <random>


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

void TextureData::init(IDevice* _device, const TextureDesc& _desc, TextureInitMode _initMode, int _initSeed)
{
    device = _device;
    desc = fixupTextureDesc(_desc);
    initMode = _initMode;
    initSeed = _initSeed;
    formatInfo = getFormatInfo(desc.format);
    REQUIRE_CALL(device->getFormatSupport(desc.format, &formatSupport));

    desc.memoryType = MemoryType::DeviceLocal;

    REQUIRE(is_set(formatSupport, FormatSupport::Texture));

    desc.usage = TextureUsage::CopySource | TextureUsage::CopyDestination;

    // D3D12 needs multisampled textures to be render targets.
    if (isMultisamplingType(_desc.type))
        desc.usage |= TextureUsage::RenderTarget;

    // Only add shader resource usage if format supports loading.
    if (is_set(formatSupport, FormatSupport::ShaderLoad))
        desc.usage |= TextureUsage::ShaderResource;

    // Initializing multi-aspect textures is not supported
    if (formatInfo.hasDepth && formatInfo.hasStencil)
        initMode = TextureInitMode::None;

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
                std::mt19937 rng(initSeed);
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

void TextureData::checkEqual(ITexture* texture) const
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

    for (uint32_t layer = 0; layer < desc.getLayerCount(); ++layer)
    {
        for (uint32_t mipLevel = 0; mipLevel < desc.mipLevelCount; ++mipLevel)
        {
            const Subresource& sr = getSubresource(layer, mipLevel);

            ComPtr<ISlangBlob> blob;
            Size rowPitch;
            REQUIRE_CALL(textureImpl->getDevice()->readTexture(textureImpl, layer, mipLevel, blob.writeRef(), &rowPitch)
            );

            for (uint32_t row = 0; row < sr.layout.rowCount; row++)
            {
                const uint8_t* expectedData = sr.data.get() + row * sr.layout.strideY;
                const uint8_t* actualData = (const uint8_t*)blob->getBufferPointer() + row * rowPitch;
                CHECK_EQ(memcmp(expectedData, actualData, sr.layout.strideY), 0);
            }
        }
    }
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
                    testTexture.desc.arrayLength = 2;
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
            desc.size = {128, 1, 1};
            break;
        case rhi::TextureType::Texture2D:
        case rhi::TextureType::Texture2DArray:
        case rhi::TextureType::Texture2DMS:
        case rhi::TextureType::Texture2DMSArray:
            desc.size = {128, 64, 1};
            break;
        case rhi::TextureType::Texture3D:
            desc.size = {128, 64, 4};
            break;
        case rhi::TextureType::TextureCube:
        case rhi::TextureType::TextureCubeArray:
            desc.size = {128, 128, 1};
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
            desc.arrayLength = max(desc.arrayLength, 2U);
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
        for (Format format : kStandardFormats)
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

        // Skip if device doesn't support format.
        FormatSupport support;
        m_device->getFormatSupport(format, &support);
        if (!is_set(support, FormatSupport::Texture))
            return;

        const FormatInfo& info = getFormatInfo(format);

        // TODO: Fix compressed format test on metal. Was seeing fatal error:
        // 'Linear textures do not support compressed pixel formats'.
        if (m_device->getDeviceType() == DeviceType::Metal && (info.isCompressed || info.hasDepth || info.hasStencil))
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
