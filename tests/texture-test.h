#pragma once

#include "testing.h"

#include "core/short_vector.h"

#include <vector>
#include <type_traits>
#include <functional>
#include <memory>

namespace rhi::testing {

/// How to initialize texture data.
enum class TextureInitMode
{
    None,    // Don't initialize
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


typedef std::function<void(int, TextureTestVariant)> GeneratorFunc;
typedef std::vector<GeneratorFunc> GeneratorList;


/// Options + variant list for running a set of texture tests.
class TextureTestOptions
{
public:
    TextureTestOptions(IDevice* device, int numTextures = 1)
        : m_device(device)
        , m_numTextures(numTextures)
    {
    }

    /// Manually add a specific variant.
    void addVariant(TextureTestVariant variant) { m_variants.push_back(variant); }

    /// Get all variants to test.
    std::vector<TextureTestVariant>& getVariants() { return m_variants; }

    /// Generate a full matrix of variants given a set of constraints:
    /// - TextureTestVariant/TestTextureDesc/TextureDesc: Explicitly specify descriptors
    /// - Format or vector<Format>: Explicit list of formats (defaults to standard list)
    /// - TTShape: Flags defining which texture types to test (1D/2D/3D/Cube)
    /// - TextureType: Explicitly specify texture type to test
    /// - TexTypes: Explicitly specify a list of texture types to test
    /// - TTMip: Whether to test with and/or without mips (for types that support it)
    /// - TTArray: Whether to test with and/or without arrays (for types that support it)
    /// - TTMS: Whether to test with and/or without multi-sample (for types that support it)
    template<typename... Args>
    void addVariants(Args... args)
    {
        // Create new list of generators for this variant set
        m_generator_lists.push_back(GeneratorList());

        // Unroll variant args, which appends a generator for each arg to the new list
        (processVariantArg(args), ...);

        // Add the post processor to generator list
        addGenerator([this](int state, TextureTestVariant variant) { postProcessVariant(state, variant); });

        // Add the filter for invalid format combinations
        addGenerator([this](int state, TextureTestVariant variant) { filterFormat(state, variant); });
    }

    /// Get current device.
    IDevice* getDevice() const { return m_device; }

    void run(std::function<void(const TextureTestVariant&)> func);

private:
    IDevice* m_device;
    int m_numTextures;
    std::vector<TextureTestVariant> m_variants;

    std::vector<GeneratorList> m_generator_lists;

    int m_current_list_idx;
    std::function<void(const TextureTestVariant&)> m_current_callback;

    void executeGeneratorList(int listIdx);

    void next(int nextIndex, TextureTestVariant variant);

    void addGenerator(GeneratorFunc generator);

    void processVariantArg(TextureDesc baseDesc);

    void processVariantArg(TestTextureDesc baseDesc);

    void processVariantArg(TextureTestVariant baseDesc);

    void processVariantArg(TextureInitMode initMode);

    void processVariantArg(TTShape shape);

    void processVariantArg(TextureType type);

    void processVariantArg(TexTypes& types);

    void processVariantArg(TTMip mip);

    void processVariantArg(TTArray array);

    void processVariantArg(TTMS multisample);

    void processVariantArg(Format format);

    void processVariantArg(const std::vector<Format>& formats);

    void postProcessVariant(int state, TextureTestVariant variant);

    void filterFormat(int state, TextureTestVariant variant);
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
    ComPtr<ITexture> getTexture(int index = 0) const { return m_textures[index]; }
    const TextureData& getTextureData(int index = 0) const { return m_datas[index]; }
    TextureData& getTextureData(int index = 0) { return m_datas[index]; }

private:
    IDevice* m_device;
    std::vector<ComPtr<ITexture>> m_textures;
    std::vector<TextureData> m_datas;
};

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

/// Texture types that can support depth formats
inline bool supportsDepthFormats(const TextureDesc& desc)
{
    switch (desc.type)
    {
    case TextureType::Texture2D:
    case TextureType::Texture2DArray:
        return true;
    default:
        return false;
    }
}

inline bool isMultisamplingType(TextureType type)
{
    switch (type)
    {
    case TextureType::Texture2DMS:
    case TextureType::Texture2DMSArray:
        return true;
    default:
        return false;
    }
}

/// Whether a format should be used with multisampling
inline bool formatSupportsMultisampling(Format format)
{
    switch (format)
    {
    case Format::RGBA8Unorm:
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
inline void runTextureTest(TextureTestOptions& options, Func&& func, Args&&... args)
{
    options.run(
        [&func, &options, &args...](const TextureTestVariant& variant)
        {
            TextureTestContext context(options.getDevice());
            for (auto& desc : variant.descriptors)
            {
                TextureData data;
                data.init(options.getDevice(), desc.desc, desc.initMode);
                REQUIRE_CALL(context.addTexture(std::move(data)));
            }
            func(&context, std::forward<Args>(args)...);
        }
    );
}

} // namespace rhi::testing
