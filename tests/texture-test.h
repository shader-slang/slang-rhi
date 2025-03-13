#pragma once

#include "testing.h"

#include <vector>
#include <type_traits>
#include <functional>
#include <memory>

namespace rhi::testing {

/// How to initialize texture data.
enum class TextureInitMode
{
    Zeros,   // Start with 0s
    Invalid, // Start with 0xcd
    Random,  // Start with deterministic random data
    MipLevel // Set each byte to its mip level
};

/// CPU equivalent of a texture, along with helpers to create textures
/// from it, and compare against other data.
struct TextureData
{
    struct Subresource
    {
        uint32_t mipLevel;
        uint32_t layer;
        std::unique_ptr<uint8_t[]> data;
        SubresourceData subresourceData;
        SubresourceLayout layout;
    };

    IDevice* device;
    TextureDesc desc;
    FormatInfo formatInfo;
    FormatSupport formatSupport;
    TextureInitMode initMode;
    int initSeed;
    std::vector<Subresource> subresources;
    std::vector<SubresourceData> subresourceData;

    void init(IDevice* device, const TextureDesc& desc, TextureInitMode initMode, int initSeed = 0);

    Result createTexture(ITexture** texture) const;

    void checkEqual(ITexture* texture) const;

    const Subresource& getSubresource(uint32_t layer, uint32_t mipLevel) const
    {
        return subresources[layer * desc.mipLevelCount + mipLevel];
    }
};

/// Description of a given texture in a variant (texture descriptor + how to init)
struct TestTextureDesc
{
    TextureDesc desc;
    TextureInitMode initMode;
};

/// Description of a given variant to test.
struct TextureTestVariant
{
    std::vector<TestTextureDesc> descriptors;
};

enum class TTShape
{
    D1 = 1 << 0,
    D2 = 1 << 1,
    D3 = 1 << 2,
    Cube = 1 << 3,
    All = D1 | D2 | D3 | Cube,
};
SLANG_RHI_ENUM_CLASS_OPERATORS(TTShape);

enum class TTMip
{
    Off = 1 << 0,
    On = 1 << 1,
    Both = Off | On,
};
SLANG_RHI_ENUM_CLASS_OPERATORS(TTMip);

enum class TTArray
{
    Off = 1 << 0,
    On = 1 << 1,
    Both = Off | On,
};
SLANG_RHI_ENUM_CLASS_OPERATORS(TTArray);

enum class TTMS
{
    Off = 1 << 0,
    On = 1 << 1,
    Both = Off | On,
};
SLANG_RHI_ENUM_CLASS_OPERATORS(TTMS);

struct TexTypes
{
    TexTypes(std::initializer_list<TextureType> shapes)
        : values(shapes) {};

    std::vector<TextureType> values;
};

/// Checks if a descriptor is a valid combination for the current device.
bool isValidDescriptor(IDevice* device, const TextureDesc& desc);

/// Checks and gets corresponding array type for texture type
bool getArrayType(TextureType type, TextureType& outArrayType);

/// Checks and gets corresponding multisample type for texture type
bool getMultisampleType(TextureType type, TextureType& outArrayType);

/// Intermediate structure during variant generation. Value of -1 for a field
/// means it is yet to be defined.
struct VariantGen
{
    int type{-1};
    int mip{-1};
    int array{-1};
    int multisample{-1};
};

/// Options + variant list for running a set of texture tests.
class TextureTestOptions
{
public:
    TextureTestOptions(IDevice* device, int numTextures = 1, TextureInitMode* initMode = nullptr)
        : m_device(device)
        , m_numTextures(numTextures)
    {
        m_initMode.resize(numTextures);
        if (initMode)
        {
            for (int i = 0; i < numTextures; i++)
                m_initMode[i] = initMode[i];
        }
        else
        {
            for (int i = 0; i < numTextures; i++)
                m_initMode[i] = TextureInitMode::Random;
        }
    }

    TextureTestOptions(IDevice* device, TextureInitMode initMode = TextureInitMode::Random)
        : m_device(device)
        , m_numTextures(1)
    {
        m_initMode.push_back(initMode);
    }

    /// Manually add a specific variant.
    void addVariant(TextureTestVariant variant) { m_variants.push_back(variant); }

    /// Get all variants to test.
    std::vector<TextureTestVariant>& getVariants() { return m_variants; }

    /// Generate a full matrix of variants given a set of constraints:
    /// - TTShape: Flags defining which texture types to test (1D/2D/3D/Cube)
    /// - TextureType: Explicitly specify texture type to test
    /// - TexTypes: Explicitly specify a list of texture types to test
    /// - TTMip: Whether to test with and/or without mips (for types that support it)
    /// - TTArray: Whether to test with and/or without arrays (for types that support it)
    /// - TTMS: Whether to test with and/or without muilti-sample (for types that support it)
    template<typename... Args>
    void addVariants(Args... args)
    {
        std::vector<VariantGen> variants;
        variants.push_back(VariantGen());
        (processVariantArg(variants, args), ...);
        addProcessedVariants(variants);
    }

    /// Get current device.
    IDevice* getDevice() const { return m_device; }

private:
    IDevice* m_device;
    int m_numTextures;
    std::vector<TextureInitMode> m_initMode;
    std::vector<TextureTestVariant> m_variants;


    void processVariantArg(std::vector<VariantGen>& variants, TTShape shape)
    {
        std::vector<VariantGen> newvariants;
        for (VariantGen variant : variants)
        {
            SLANG_RHI_ASSERT(variant.type == -1);
            if (is_set(shape, TTShape::D1))
            {
                variant.type = (int)TextureType::Texture1D;
                newvariants.push_back(variant);
            }
            if (is_set(shape, TTShape::D2))
            {
                variant.type = (int)TextureType::Texture2D;
                newvariants.push_back(variant);
            }
            if (is_set(shape, TTShape::D3))
            {
                variant.type = (int)TextureType::Texture3D;
                newvariants.push_back(variant);
            }
            if (is_set(shape, TTShape::Cube))
            {
                variant.type = (int)TextureType::TextureCube;
                newvariants.push_back(variant);
            }
        }
        variants = newvariants;
    }

    void processVariantArg(std::vector<VariantGen>& variants, TextureType type)
    {
        std::vector<VariantGen> newvariants;
        for (VariantGen variant : variants)
        {
            SLANG_RHI_ASSERT(variant.type == -1);
            variant.type = (int)type;
            newvariants.push_back(variant);
        }
        variants = newvariants;
    }

    void processVariantArg(std::vector<VariantGen>& variants, TexTypes& types)
    {
        std::vector<VariantGen> newvariants;
        for (VariantGen variant : variants)
        {
            SLANG_RHI_ASSERT(variant.type == -1);
            for (TextureType type : types.values)
            {
                variant.type = (int)type;
                newvariants.push_back(variant);
            }
        }
        variants = newvariants;
    }

    void processVariantArg(std::vector<VariantGen>& variants, TTMip mip)
    {
        std::vector<VariantGen> newvariants;
        for (VariantGen variant : variants)
        {
            SLANG_RHI_ASSERT(variant.mip == -1);
            if (is_set(mip, TTMip::Off))
            {
                variant.mip = 0;
                newvariants.push_back(variant);
            }
            if (is_set(mip, TTMip::On))
            {
                variant.mip = 1;
                newvariants.push_back(variant);
            }
        }
        variants = newvariants;
    }

    void processVariantArg(std::vector<VariantGen>& variants, TTArray array)
    {
        std::vector<VariantGen> newvariants;
        for (VariantGen variant : variants)
        {
            SLANG_RHI_ASSERT(variant.array == -1);
            if (is_set(array, TTArray::Off))
            {
                variant.array = 0;
                newvariants.push_back(variant);
            }
            if (is_set(array, TTArray::On))
            {
                variant.array = 1;
                newvariants.push_back(variant);
            }
        }
        variants = newvariants;
    }

    void processVariantArg(std::vector<VariantGen>& variants, TTMS multisample)
    {
        std::vector<VariantGen> newvariants;
        for (VariantGen variant : variants)
        {
            SLANG_RHI_ASSERT(variant.multisample == -1);
            if (is_set(multisample, TTMS::Off))
            {
                variant.multisample = 0;
                newvariants.push_back(variant);
            }
            if (is_set(multisample, TTMS::On))
            {
                variant.multisample = 1;
                newvariants.push_back(variant);
            }
        }
        variants = newvariants;
    }

    void addProcessedVariants(std::vector<VariantGen>& variants);
};

/// Context within which a given iteration of a texture test works. This
/// is passed in to the user function with pre-allocated / initialized
/// textures.
class TextureTestContext
{
public:
    TextureTestContext(IDevice* device);

    Result addTexture(TextureData&& data);

    IDevice* getDevice() const { return m_device; }
    ComPtr<ITexture> getTexture(int index) const { return m_textures[index]; }
    const TextureData& getTextureData(int index) const { return m_datas[index]; }

private:
    IDevice* m_device;
    std::vector<ComPtr<ITexture>> m_textures;
    std::vector<TextureData> m_datas;
};

/// Formats not currently handled
/// TODO(testing): Format selection should be part of test variant generation.
inline bool shouldIgnoreFormat(Format format)
{
    switch (format)
    {
    case Format::D16Unorm:
    case Format::D32FloatS8Uint:
    case Format::D32Float:
        return true;
    default:
        break;
    }

    return false;
}

/// Texture types that can support compressed data
inline bool supportsCompressedFormats(const TextureDesc& desc)
{
    switch (desc.type)
    {
    case TextureType::Texture2D:
    case TextureType::Texture2DArray:
    case TextureType::TextureCube:
    case TextureType::TextureCubeArray:
        // case TextureType::Texture3D: //TODO: Potentially Re-enable for D3D - it highlighted some bugs!
        return true;
    default:
        return false;
    }
}

/// Run a texture test.
/// - func: should be a callable of the form void(TextureTestContext*, Args...)
/// - args: 0 or more user defined arguments that are forwarded to the function
/// The test function will be called multiple times with pre-allocated and
/// initialized textures, as per the TextureTestOptions structure.
template<typename Func, typename... Args>
inline void runTextureTest(TextureTestOptions options, Func&& func, Args&&... args)
{
    // Nice selection of formats to test
    Format formats[] = {
        Format::RGBA32Uint,
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

    // Change this to run against every format
    // for (int f = 0; f < (int)Format::_Count; f++)
    //{
    //    Format format = (Format)f;
    for (Format format : formats)
    {
        IDevice* device = options.getDevice();

        FormatSupport support;
        device->getFormatSupport(format, &support);
        if (!is_set(support, FormatSupport::Texture))
            continue;

        const FormatInfo& info = getFormatInfo(format);

        if (shouldIgnoreFormat(format))
            continue;

        // TODO: Fix compressed format test on metal. Was seeing fatal error:
        // 'Linear textures do not support compressed pixel formats'.
        if (device->getDeviceType() == DeviceType::Metal && info.isCompressed)
            continue;

        for (auto& variant : options.getVariants())
        {
            TextureDesc& td = variant.descriptors[0].desc;
            CAPTURE(td.type);
            CAPTURE(td.size.width);
            CAPTURE(td.size.height);
            CAPTURE(td.size.depth);
            CAPTURE(td.mipLevelCount);
            CAPTURE(td.arrayLength);
            CAPTURE(td.format);

            if (info.isCompressed)
            {
                if (!supportsCompressedFormats(td))
                    continue;
            }

            TextureTestContext context(device);
            for (auto& desc : variant.descriptors)
            {
                TextureData data;
                desc.desc.format = format;
                data.init(device, desc.desc, desc.initMode);
                REQUIRE_CALL(context.addTexture(std::move(data)));
            }


            func(&context, std::forward<Args>(args)...);
        }
    }
}

} // namespace rhi::testing
