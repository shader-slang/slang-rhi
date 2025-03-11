#include "texture-test.h"


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

} // namespace rhi::testing
