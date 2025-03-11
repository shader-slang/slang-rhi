#pragma once

#include "testing.h"

#include <vector>
#include <type_traits>
#include <functional>

namespace rhi::testing {

struct TestTexture : public RefObject
{
    ComPtr<ITexture> texture;
    std::vector<SubresourceData> initData;
    std::vector<SubresourceLayout> layout;
};

enum class TextureInitMode
{
    None,
    Clear,
    Random
};

struct TestTextureDesc
{
    TextureDesc desc;
    TextureInitMode initMode;
};

Result createTestTexture(TestTextureDesc desc, TestTexture** outTestTexture);

void checkTextureEqual(RefPtr<TestTexture> a, RefPtr<TestTexture> b);

void checkTextureEqual(
    RefPtr<TestTexture> a,
    int aLayerIdx,
    int aMipLevel,
    RefPtr<TestTexture> b,
    int bLayerIdx,
    int bMipLevel
);

struct TextureTestConfig
{};

class TextureTestContext
{};

template<typename... Args>
inline void runTextureTestStep(
    TextureTestContext* context,
    std::function<void(TextureTestContext*, Args...)> func,
    Args&&... args
)
{
    func(context, std::forward<Args>(args)...);
}

struct TextureTestVariant
{
    std::vector<TestTextureDesc> descriptors;
};

enum class TextureTestShapeFlags
{
    S1D = 1 << 0,
    S2D = 1 << 1,
    S3D = 1 << 2,
    SCube = 1 << 3,
    SAll = S1D | S2D | S3D | SCube,
};
SLANG_RHI_ENUM_CLASS_OPERATORS(TextureTestShapeFlags);

enum class TextureTestMipFlags
{
    MipOff = 1 << 0,
    MipOn = 1 << 1,
    MipBoth = MipOff | MipOn,
};
SLANG_RHI_ENUM_CLASS_OPERATORS(TextureTestMipFlags);

enum class TextureTestArrayFlags
{
    ArrayOff = 1 << 0,
    ArrayOn = 1 << 1,
    ArrayBoth = ArrayOff | ArrayOn,
};
SLANG_RHI_ENUM_CLASS_OPERATORS(TextureTestArrayFlags);

enum class TextureTestMultisampleFlags
{
    MultisampleOff = 1 << 0,
    MultisampleOn = 1 << 1,
    MultisampleBoth = MultisampleOff | MultisampleOn,
};
SLANG_RHI_ENUM_CLASS_OPERATORS(TextureTestMultisampleFlags);

struct GenVariantsOptions
{
    IDevice* device;
    TextureTestShapeFlags shape;
    TextureTestMipFlags mip;
    TextureTestArrayFlags array;
    TextureTestMultisampleFlags multisample;
    int numTextures;
    TextureInitMode* init;
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
    }

private:
    IDevice* m_device;
    int m_numTextures;
    TextureInitMode* m_initMode;
    std::vector<TextureTestVariant> m_variants;


    void processVariantArg(std::vector<VariantGen>& variants, TextureTestShapeFlags shape)
    {
        std::vector<VariantGen> newvariants;
        for (VariantGen variant : variants)
        {
            SLANG_RHI_ASSERT(variant.type == -1);
            if (is_set(shape, TextureTestShapeFlags::S1D))
            {
                variant.type = (int)TextureType::Texture1D;
                newvariants.push_back(variant);
            }
            if (is_set(shape, TextureTestShapeFlags::S2D))
            {
                variant.type = (int)TextureType::Texture2D;
                newvariants.push_back(variant);
            }
            if (is_set(shape, TextureTestShapeFlags::S3D))
            {
                variant.type = (int)TextureType::Texture3D;
                newvariants.push_back(variant);
            }
            if (is_set(shape, TextureTestShapeFlags::SCube))
            {
                variant.type = (int)TextureType::TextureCube;
                newvariants.push_back(variant);
            }
        }
        variants = newvariants;
    }

    void processVariantArg(std::vector<VariantGen>& variants, TextureTestMipFlags mip)
    {
        std::vector<VariantGen> newvariants;
        for (VariantGen variant : variants)
        {
            SLANG_RHI_ASSERT(variant.mip == -1);
            if (is_set(mip, TextureTestMipFlags::MipOff))
            {
                variant.mip = 0;
                newvariants.push_back(variant);
            }
            if (is_set(mip, TextureTestMipFlags::MipOff))
            {
                variant.mip = 1;
                newvariants.push_back(variant);
            }
        }
        variants = newvariants;
    }

    void processVariantArg(std::vector<VariantGen>& variants, TextureTestArrayFlags array)
    {
        std::vector<VariantGen> newvariants;
        for (VariantGen variant : variants)
        {
            SLANG_RHI_ASSERT(variant.array == -1);
            if (is_set(array, TextureTestArrayFlags::ArrayOff))
            {
                variant.array = 0;
                newvariants.push_back(variant);
            }
            if (is_set(array, TextureTestArrayFlags::ArrayOn))
            {
                variant.array = 1;
                newvariants.push_back(variant);
            }
        }
        variants = newvariants;
    }

    void processVariantArg(std::vector<VariantGen>& variants, TextureTestMultisampleFlags multisample)
    {
        std::vector<VariantGen> newvariants;
        for (VariantGen variant : variants)
        {
            SLANG_RHI_ASSERT(variant.multisample == -1);
            if (is_set(multisample, TextureTestMultisampleFlags::MultisampleOff))
            {
                variant.multisample = 0;
                newvariants.push_back(variant);
            }
            if (is_set(multisample, TextureTestMultisampleFlags::MultisampleOn))
            {
                variant.multisample = 1;
                newvariants.push_back(variant);
            }
        }
        variants = newvariants;
    }

    void addProcessedVariants(std::vector<VariantGen>& variants);
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
            TextureTestContext context;
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
