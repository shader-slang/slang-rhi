#pragma once

#include "testing.h"

#include <vector>
#include <type_traits>
#include <functional>
#include <memory>

namespace rhi::testing {

struct TestTexture : public RefObject
{
    ComPtr<ITexture> texture;
    std::vector<SubresourceLayout> layout;
};

enum class TextureInitMode
{
    Zeros,   // Start with 0s
    Invalid, // Start with 0xcd
    Random   // Start with deterministic random data
};

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

    TextureDesc desc;
    FormatInfo formatInfo;
    TextureInitMode initMode;
    int initSeed;
    std::vector<Subresource> subresources;
    std::vector<SubresourceData> subresourceData;

    void init(const TextureDesc& desc, TextureInitMode initMode, int initSeed = 0);

    Result createTexture(IDevice* device, ITexture** texture) const;

    void checkEqual(ComPtr<ITexture> texture) const;

    const Subresource& getSubresource(uint32_t layer, uint32_t mipLevel) const
    {
        return subresources[layer * desc.mipLevelCount + mipLevel];
    }
};


struct TestTextureDesc
{
    TextureDesc desc;
    TextureInitMode initMode;
};

void checkTextureEqual(RefPtr<TestTexture> a, RefPtr<TestTexture> b);

void checkTextureEqual(
    RefPtr<TestTexture> a,
    int aLayerIdx,
    int aMipLevel,
    RefPtr<TestTexture> b,
    int bLayerIdx,
    int bMipLevel
);


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
    TextureTestOptions(IDevice* device, int numTextures, TextureInitMode* initMode = nullptr)
        : m_device(device)
        , m_numTextures(numTextures)
        , m_initMode(initMode)
    {
    }

    void addVariant(TextureTestVariant variant) { m_variants.push_back(variant); }

    std::vector<TextureTestVariant>& getVariants() { return m_variants; }

    template<typename... Args>
    void addVariants(Args... args)
    {
        std::vector<VariantGen> variants;
        variants.push_back(VariantGen());
        (processVariantArg(variants, args), ...);
        addProcessedVariants(variants);
    }

    IDevice* getDevice() const { return m_device; }

private:
    IDevice* m_device;
    int m_numTextures;
    TextureInitMode* m_initMode;
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

class TextureTestContext
{
public:
    TextureTestContext(IDevice* device);

    Result addTexture(TextureData&& data);

    ComPtr<ITexture> getTexture(int index) const { return m_textures[index]; }
    const TextureData& getTextureData(int index) const { return m_datas[index]; }

private:
    IDevice* m_device;
    std::vector<ComPtr<ITexture>> m_textures;
    std::vector<TextureData> m_datas;
};


/// Run a texture test.
/// - func: should be a callable of the form void(TextureTestContext*, Args...)
/// - args: 0 or more user defined arguments that are forwarded to the function
/// The test function will be called multiple times with pre-allocated and
/// initialized textures, as per the TextureTestOptions structure.
template<typename Func, typename... Args>
inline void runTextureTest(TextureTestOptions options, Func&& func, Args&&... args)
{
    auto formats = {Format::R32G32B32A32_FLOAT};

    for (auto& format : formats)
    {
        for (auto& variant : options.getVariants())
        {
            TextureTestContext context(options.getDevice());
            for (auto& desc : variant.descriptors)
            {
                TextureData data;
                desc.desc.format = format;
                data.init(desc.desc, desc.initMode);
                context.addTexture(std::move(data));
            }

            TextureDesc firstTextureDesc = context.getTexture(0)->getDesc();
            CAPTURE(firstTextureDesc.type);
            CAPTURE(firstTextureDesc.size.width);
            CAPTURE(firstTextureDesc.size.height);
            CAPTURE(firstTextureDesc.size.depth);
            CAPTURE(firstTextureDesc.mipLevelCount);
            CAPTURE(firstTextureDesc.arrayLength);
            CAPTURE(firstTextureDesc.format);

            func(&context, std::forward<Args>(args)...);
        }
    }
}


/*
void runTextureTest2(std::vector<TestTextureDesc> descriptors, TextureTest* test)
{
    TextureTestContext context;


    TextureTestOptions options;
    options.init(S1D | S2D);

    //... can populate options here that tell test how to run
    runTextureTest(options, myTestFunc, 1);
}
*/


} // namespace rhi::testing
