#include "texture-test.h"
#include <cmath>
#include "core/common.h"
#include "rhi-shared.h"
#include <random>


namespace rhi::testing {

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
    if (device->getDeviceType() == DeviceType::Metal &&
        (desc.type == TextureType::Texture2DMS || desc.type == TextureType::Texture2DMSArray) && desc.sampleCount == 1)
        return false;
    // CUDA does not support multisample textures.
    if (device->getDeviceType() == DeviceType::CUDA &&
        (desc.type == TextureType::Texture2DMS || desc.type == TextureType::Texture2DMSArray))
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


void TextureData::init(const TextureDesc& _desc, TextureInitMode _initMode, int _initSeed)
{
    desc = fixupTextureDesc(_desc);
    initMode = _initMode;
    initSeed = _initSeed;
    formatInfo = getFormatInfo(desc.format);

    desc.memoryType = MemoryType::DeviceLocal;
    desc.usage = TextureUsage::ShaderResource | TextureUsage::CopySource | TextureUsage::CopyDestination;

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

Result TextureData::createTexture(IDevice* device, ITexture** texture) const
{
    return device->createTexture(desc, subresourceData.data(), texture);
}

void TextureData::checkEqual(ComPtr<ITexture> texture) const
{
    Texture* textureImpl = checked_cast<Texture*>(texture.get());

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
            textureImpl->getDevice()->readTexture(textureImpl, layer, mipLevel, blob.writeRef(), &rowPitch);

            for (uint32_t row = 0; row < sr.layout.rowCount; row++)
            {
                const uint8_t* expectedData = sr.data.get() + row * sr.layout.strideY;
                const uint8_t* actualData = (const uint8_t*)blob->getBufferPointer() + row * rowPitch;
                CHECK_EQ(memcmp(expectedData, actualData, sr.layout.strideY), 0);
            }
        }
    }
}

void TextureTestOptions::addProcessedVariants(std::vector<VariantGen>& variants)
{
    // Go through all created variants, post process them and add if valid.
    for (VariantGen variant : variants)
    {
        SLANG_RHI_ASSERT(variant.type != -1); // types must be specified

        // Defaults for arrays, mips and multisample are all off
        if (variant.array == -1)
            variant.array = 0;
        if (variant.mip == -1)
            variant.mip = 0;
        if (variant.multisample == -1)
            variant.multisample = 0;

        // Start with the declared type.
        TextureType baseType = (TextureType)variant.type;

        // If user has explicitly made it an array, switch to array type.
        // Note: has no effect if type already explicitly an array.
        if (variant.array == 1)
        {
            TextureType arrayType;
            if (!getArrayType(baseType, arrayType))
                continue;
            baseType = arrayType;
        }

        // If user has explicitly made it multisampled, switch to multisampled type.
        // Note: has no effect if type already explicitly multisampled.
        if (variant.multisample == 1)
        {
            TextureType multisampleType;
            if (!getMultisampleType(baseType, multisampleType))
                continue;
            baseType = multisampleType;
        }

        // Build descriptor
        TextureDesc desc;
        desc.type = baseType;

        // Set size based on type.
        switch (baseType)
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

        // Set array size for any of the array types.
        switch (baseType)
        {
        case rhi::TextureType::Texture1DArray:
        case rhi::TextureType::Texture2DArray:
        case rhi::TextureType::Texture2DMSArray:
        case rhi::TextureType::TextureCubeArray:
            desc.arrayLength = 4;
        default:
            break;
        }

        // Set mip level count.
        desc.mipLevelCount = variant.mip ? kAllMipLevels : 1;

        // If ended up with a descriptor for a valid texture for this
        // platform, generate variant info for it and store.
        if (isValidDescriptor(m_device, desc))
        {
            TextureTestVariant newVariant;
            for (int i = 0; i < m_numTextures; i++)
            {
                auto mode = m_initMode ? m_initMode[i] : TextureInitMode::Random;
                newVariant.descriptors.push_back({desc, mode});
            }
            addVariant(newVariant);
        }
    }
}

TextureTestContext::TextureTestContext(IDevice* device)
    : m_device(device)
{
}

Result TextureTestContext::addTexture(TextureData&& data)
{
    ComPtr<ITexture> texture;
    SLANG_RETURN_ON_FAIL(data.createTexture(m_device, texture.writeRef()));
    m_textures.push_back(texture);
    m_datas.push_back(std::move(data));
    return SLANG_OK;
}

} // namespace rhi::testing
