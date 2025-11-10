#include "testing.h"
#include "texture-test.h"
#include "format-conversion.h"
#include <map>

// This series of tests tests reading/writing texture from shaders.
// The following is covered:
// - reading from textures using both Load() and the subscript operator
//   - read-only textures:
//     - all layers/all mips (Load() only)
//     - all layers/single mip (Load() and subscript load operator)
//     - single layer/single mip (Load() and subscript load operator)
//   - read-write textures:
//     - all layers/single mip (Load() and subscript load operator)
//     - single layer/single mip (Load() and subscript load operator)
// - writing to textures using both Store() and the subscript operator
//   - all layers/single mip (Store() and subscript store operator)
//   - single layer/single mip (Store() and subscript store operator)

#define TEST_SPECIFIC_FORMATS 0

using namespace rhi;
using namespace rhi::testing;

inline std::string getTextureType(TextureType type)
{
    switch (type)
    {
    case TextureType::Texture1D:
        return "Texture1D";
    case TextureType::Texture1DArray:
        return "Texture1DArray";
    case TextureType::Texture2D:
        return "Texture2D";
    case TextureType::Texture2DArray:
        return "Texture2DArray";
    case TextureType::Texture2DMS:
        return "Texture2DMS";
    case TextureType::Texture2DMSArray:
        return "Texture2DMSArray";
    case TextureType::Texture3D:
        return "Texture3D";
    case TextureType::TextureCube:
        return "TextureCube";
    case TextureType::TextureCubeArray:
        return "TextureCubeArray";
    default:
        FAIL("Unknown texture type");
    }
    return "";
};

inline std::string getRWTextureType(TextureType type)
{
    switch (type)
    {
    case TextureType::Texture1D:
        return "RWTexture1D";
    case TextureType::Texture1DArray:
        return "RWTexture1DArray";
    case TextureType::Texture2D:
        return "RWTexture2D";
    case TextureType::Texture2DArray:
        return "RWTexture2DArray";
    case TextureType::Texture2DMS:
        return "RWTexture2DMS";
    case TextureType::Texture2DMSArray:
        return "RWTexture2DMSArray";
    case TextureType::Texture3D:
        return "RWTexture3D";
    case TextureType::TextureCube:
    case TextureType::TextureCubeArray:
        FAIL("Unsupported texture type");
        break;
    default:
        FAIL("Unknown texture type");
    }
    return "";
};

inline std::string getFormatType(Format format)
{
    const FormatInfo& info = getFormatInfo(format);

    std::string type;

    switch (info.kind)
    {
    case FormatKind::Integer:
        type += info.isSigned ? "int" : "uint";
        break;
    case FormatKind::Normalized:
        type += "float";
        break;
    case FormatKind::Float:
        type += "float";
        break;
    case FormatKind::DepthStencil:
        break;
    }

    if (info.channelCount > 1)
        type += std::to_string(info.channelCount);

    return type;
}

inline std::string getFormatAttribute(Format format)
{
    const FormatInfo& info = getFormatInfo(format);

    if (info.slangName)
    {
        return "[format(\"" + std::string(info.slangName) + "\")] ";
    }
    else
    {
        return "";
    }
}

#if TEST_SPECIFIC_FORMATS
static const std::vector<Format> kFormats = {
    // 8-bit / 1-channel formats
    Format::R8Uint,
    Format::R8Sint,
    Format::R8Unorm,
    Format::R8Snorm,
    // 8-bit / 2-channel formats
    Format::RG8Uint,
    Format::RG8Sint,
    Format::RG8Unorm,
    Format::RG8Snorm,
    // 8-bit / 4-channel formats
    Format::RGBA8Uint,
    Format::RGBA8Sint,
    Format::RGBA8Unorm,
    Format::RGBA8UnormSrgb,
    Format::RGBA8Snorm,
    Format::BGRA8Unorm,
    Format::BGRA8UnormSrgb,
    // These currently fail due to last channel
    Format::BGRX8Unorm,
    Format::BGRX8UnormSrgb,
    // 16-bit / 1-channel formats
    Format::R16Uint,
    Format::R16Sint,
    Format::R16Unorm,
    Format::R16Snorm,
    Format::R16Float,
    // 16-bit / 2-channel formats
    Format::RG16Uint,
    Format::RG16Sint,
    Format::RG16Unorm,
    Format::RG16Snorm,
    Format::RG16Float,
    // 16-bit / 4-channel formats
    Format::RGBA16Uint,
    Format::RGBA16Sint,
    Format::RGBA16Unorm,
    Format::RGBA16Snorm,
    Format::RGBA16Float,
    // 32-bit / 1-channel formats
    Format::R32Uint,
    Format::R32Sint,
    Format::R32Float,
    // 32-bit / 2-channel formats
    Format::RG32Uint,
    Format::RG32Sint,
    Format::RG32Float,
    // 32-bit / 4-channel formats
    Format::RGBA32Uint,
    Format::RGBA32Sint,
    Format::RGBA32Float,
    // Mixed formats
    Format::BGRA4Unorm,
    Format::B5G6R5Unorm,
    Format::BGR5A1Unorm,
    Format::RGB10A2Uint,
    Format::RGB10A2Unorm,
};
#endif

inline bool shouldSkipFormat(Format format)
{
    switch (format)
    {
    case Format::RGB9E5Ufloat:
    case Format::R11G11B10Float:
        return true;
    default:
        return false;
    }
}

inline bool needsFormatConversion(Format format)
{
    const FormatInfo& info = getFormatInfo(format);
    return info.kind == FormatKind::Normalized || info.kind == FormatKind::Float ||
           (info.kind == FormatKind::Integer && info.blockSizeInBytes / info.channelCount != 4);
}


struct TexelData
{
    uint32_t layer;
    uint32_t mip;
    uint32_t offset[3];
    union
    {
        float floats[4];
        int32_t ints[4];
        uint32_t uints[4];
    };
    uint8_t raw[16];
};

static void clearTexelDataValues(span<TexelData> texels)
{
    for (TexelData& texel : texels)
    {
        memset(texel.floats, 0, 16);
        memset(texel.raw, 0, 16);
    }
}

static void compareTexelData(Format format, span<TexelData> a, span<TexelData> b)
{
    REQUIRE(a.size() == b.size());
    const FormatInfo& info = getFormatInfo(format);

    float tolerance[4] = {0.f, 0.f, 0.f, 0.f};
    if (info.kind == FormatKind::Normalized)
    {
        int bits = (info.blockSizeInBytes * 8) / info.channelCount;
        std::array<int, 4> channelBits = {bits, bits, bits, bits};
        switch (format)
        {
        case Format::B5G6R5Unorm:
            channelBits = {5, 6, 5, 0};
            break;
        case Format::BGR5A1Unorm:
            channelBits = {5, 5, 5, 1};
            break;
        case Format::RGB10A2Unorm:
            channelBits = {10, 10, 10, 2};
            break;
        default:
            break;
        }
        for (int i = 0; i < 4; ++i)
        {
            if (channelBits[i] > 0)
                tolerance[i] = 1.f / ((1 << channelBits[i]) - 1);
            if (info.isSigned)
                tolerance[i] *= 2;
            if (info.isSrgb)
                tolerance[i] *= 2; // sRGB conversion is not exact
        }
    }

    for (size_t i = 0; i < a.size(); ++i)
    {
        CAPTURE(i);
        const TexelData& texelA = a[i];
        const TexelData& texelB = b[i];
        REQUIRE(texelA.layer == texelB.layer);
        REQUIRE(texelA.mip == texelB.mip);
        REQUIRE(texelA.offset[0] == texelB.offset[0]);
        REQUIRE(texelA.offset[1] == texelB.offset[1]);
        REQUIRE(texelA.offset[2] == texelB.offset[2]);
        uint32_t channelCount = info.channelCount;
        // Ignore last channel for BGRX formats
        if (format == Format::BGRX8Unorm || format == Format::BGRX8UnormSrgb)
            channelCount = 3;
        for (uint32_t c = 0; c < channelCount; ++c)
        {
            if (info.kind == FormatKind::Integer && info.isSigned)
            {
                CHECK(texelA.ints[c] == texelB.ints[c]);
            }
            else if (info.kind == FormatKind::Integer && !info.isSigned)
            {
                CHECK(texelA.uints[c] == texelB.uints[c]);
            }
            else if (info.kind == FormatKind::Normalized)
            {

                CHECK(texelA.floats[c] >= texelB.floats[c] - tolerance[c]);
                CHECK(texelA.floats[c] <= texelB.floats[c] + tolerance[c]);
            }
            else if (info.kind == FormatKind::Float)
            {
                CHECK(texelA.floats[c] == texelB.floats[c]);
            }
            else
            {
                FAIL("Unsupported format");
            }
        }
    }
}

enum class TextureViewType
{
    ReadOnly,
    ReadWrite
};

enum class ReadMethod
{
    Load,
    Subscript
};

enum class WriteMethod
{
    Store,
    Subscript
};

class TextureViewTest
{
public:
    IDevice* m_device;
    ComPtr<ICommandQueue> m_queue;
    ComPtr<IBuffer> m_buffer;
    std::unique_ptr<uint8_t[]> m_readbackData;
    std::unique_ptr<uint8_t[]> m_tmpData;
    size_t m_tmpDataSize;

    using ReadPipelineKey = std::tuple<TextureViewType, TextureType, Format, ReadMethod>;
    std::map<ReadPipelineKey, ComPtr<IComputePipeline>> m_readPipelines;
    using WritePipelineKey = std::tuple<TextureType, Format, WriteMethod>;
    std::map<WritePipelineKey, ComPtr<IComputePipeline>> m_writePipelines;

    TextureViewTest(IDevice* device)
        : m_device(device)
        , m_queue(m_device->getQueue(QueueType::Graphics))
    {
        BufferDesc bufferDesc = {};
        bufferDesc.size = 4 * 1024 * 1024;
        bufferDesc.usage = BufferUsage::CopySource | BufferUsage::CopyDestination | BufferUsage::ShaderResource |
                           BufferUsage::UnorderedAccess;
        m_buffer = device->createBuffer(bufferDesc, nullptr);
        m_readbackData = std::make_unique<uint8_t[]>(bufferDesc.size);
        m_tmpDataSize = 1024 * 1024;
        m_tmpData = std::make_unique<uint8_t[]>(m_tmpDataSize);
    }

    void writeTexelsRawHost(ITextureView* textureView, span<TexelData> texels)
    {
        ITexture* texture = textureView->getTexture();
        uint32_t baseLayer = textureView->getDesc().subresourceRange.layer;
        uint32_t baseMip = textureView->getDesc().subresourceRange.mip;
        ComPtr<ICommandEncoder> commandEncoder = m_queue->createCommandEncoder();
        for (const TexelData& texel : texels)
        {
            SubresourceLayout layout;
            texture->getSubresourceLayout(baseMip + texel.mip, &layout);
            Offset3D offset = {texel.offset[0], texel.offset[1], texel.offset[2]};
            Extent3D extent = {1, 1, 1};
            SubresourceRange srRange = {baseLayer + texel.layer, 1, baseMip + texel.mip, 1};
            SLANG_RHI_ASSERT(m_tmpDataSize >= layout.rowPitch);
            std::memcpy(m_tmpData.get(), texel.raw, 16);
            SubresourceData srData = {m_tmpData.get(), layout.rowPitch, layout.slicePitch};
            commandEncoder->uploadTextureData(texture, srRange, offset, extent, &srData, 1);
        }
        m_queue->submit(commandEncoder->finish());
    }

    void writeTexelsHost(ITextureView* textureView, span<TexelData> texels)
    {
        // Pack texels to raw data
        Format format = textureView->getTexture()->getDesc().format;
        const FormatInfo& info = getFormatInfo(format);
        FormatConversionFuncs funcs = getFormatConversionFuncs(format);
        switch (info.kind)
        {
        case FormatKind::Integer:
            for (TexelData& texel : texels)
            {
                funcs.packIntFunc(texel.uints, texel.raw);
            }
            break;
        case FormatKind::Normalized:
        case FormatKind::Float:
            for (TexelData& texel : texels)
                funcs.packFloatFunc(texel.floats, texel.raw);
            break;
        case FormatKind::DepthStencil:
            FAIL("Depth/stencil not supported!");
        }
        writeTexelsRawHost(textureView, texels);
    }

    void readTexelsRawHost(ITextureView* textureView, span<TexelData> texels)
    {
        REQUIRE(texels.size_bytes() < m_buffer->getDesc().size);
        ITexture* texture = textureView->getTexture();
        uint32_t baseLayer = textureView->getDesc().subresourceRange.layer;
        uint32_t baseMip = textureView->getDesc().subresourceRange.mip;
        ComPtr<ICommandEncoder> commandEncoder = m_queue->createCommandEncoder();
        uint64_t offset = 0;
        for (const TexelData& texel : texels)
        {
            SubresourceLayout layout;
            texture->getSubresourceLayout(baseMip + texel.mip, &layout);
            Extent3D extent = {1, 1, 1};
            commandEncoder->copyTextureToBuffer(
                m_buffer,
                offset,
                16,
                layout.rowPitch,
                texture,
                baseLayer + texel.layer,
                baseMip + texel.mip,
                Offset3D{texel.offset[0], texel.offset[1], texel.offset[2]},
                extent
            );
            offset += 16;
        }
        m_queue->submit(commandEncoder->finish());
        m_device->readBuffer(m_buffer, 0, texels.size() * 16, m_readbackData.get());
        offset = 0;
        for (TexelData& texel : texels)
        {
            // Map the raw data back to the texel structure
            std::memcpy(texel.raw, m_readbackData.get() + offset, 16);
            offset += 16;
        }
    }

    void readTexelsHost(ITextureView* textureView, span<TexelData> texels)
    {
        // Unpack raw data to texels
        Format format = textureView->getTexture()->getDesc().format;
        const FormatInfo& info = getFormatInfo(format);
        FormatConversionFuncs funcs = getFormatConversionFuncs(format);
        readTexelsRawHost(textureView, texels);
        switch (info.kind)
        {
        case FormatKind::Integer:
            for (TexelData& texel : texels)
                funcs.unpackIntFunc(texel.raw, texel.uints);
            break;
        case FormatKind::Normalized:
        case FormatKind::Float:
            for (TexelData& texel : texels)
                funcs.unpackFloatFunc(texel.raw, texel.floats);
            break;
        case FormatKind::DepthStencil:
            FAIL("Depth/stencil not supported!");
        }
    }

    std::string_view getShaderPrelude()
    {
        return R"(
struct TexelData {
    uint layer;
    uint mip;
    uint offset[3];
    uint values[4];
    uint raw[4];
};
)";
    }

    ComPtr<IComputePipeline> getWritePipeline(TextureType textureType, Format format, WriteMethod writeMethod)
    {
        WritePipelineKey key = {textureType, format, writeMethod};
        auto it = m_writePipelines.find(key);
        if (it != m_writePipelines.end())
            return it->second;

        const FormatInfo& info = getFormatInfo(format);
        std::string formatType = getFormatType(format);
        std::string slangTextureType =
            getFormatAttribute(format) + " " + getRWTextureType(textureType) + "<" + formatType + ">";
        std::string source;
        source += getShaderPrelude();
        source += "[shader(\"compute\")]\n";
        source += "[numthreads(1,1,1)]\n";
        source += "void writeTexels(\n";
        source += "    uint3 tid : SV_DispatchThreadID,\n";
        source += "    uniform " + slangTextureType + " texture,\n";
        source += "    uniform RWStructuredBuffer<TexelData> texelData,\n";
        source += "    uniform uint texelCount)\n";
        source += "{\n";
        source += "    if (tid.x > texelCount)\n";
        source += "        return;\n";
        source += "    TexelData texel = texelData[tid.x];\n";
        source += "    " + formatType + " value;\n";
        std::string convertFunc = "";
        switch (info.kind)
        {
        case FormatKind::Integer:
            convertFunc = "asuint";
            break;
        case FormatKind::Normalized:
        case FormatKind::Float:
            convertFunc = "asfloat";
            break;
        case FormatKind::DepthStencil:
            break;
        }
        if (info.channelCount == 1)
        {
            source += "    value = " + convertFunc + "(texel.values[0]);\n";
        }
        else
        {
            for (uint32_t i = 0; i < info.channelCount; ++i)
            {
                source += "    value[" + std::to_string(i) + "] = " + convertFunc + "(texel.values[" +
                          std::to_string(i) + "]);\n";
            }
        }
        if (writeMethod == WriteMethod::Store)
        {
            if (textureType == TextureType::Texture1D)
            {
                source += "    texture.Store(texel.offset[0], value);\n";
            }
            if (textureType == TextureType::Texture1DArray)
            {
                source += "    texture.Store(uint2(texel.offset[0], texel.layer), value);\n";
            }
            else if (textureType == TextureType::Texture2D)
            {
                source += "    texture.Store(uint2(texel.offset[0], texel.offset[1]), value);\n";
            }
            else if (textureType == TextureType::Texture2DArray)
            {
                source += "    texture.Store(uint3(texel.offset[0], texel.offset[1], texel.layer), value);\n";
            }
            else if (textureType == TextureType::Texture3D)
            {
                source += "    texture.Store(uint3(texel.offset[0], texel.offset[1], texel.offset[2]), value);\n";
            }
        }
        else if (writeMethod == WriteMethod::Subscript)
        {
            if (textureType == TextureType::Texture1D)
            {
                source += "    texture[texel.offset[0]] = value;\n";
            }
            if (textureType == TextureType::Texture1DArray)
            {
                source += "    texture[uint2(texel.offset[0], texel.layer)] = value;\n";
            }
            else if (textureType == TextureType::Texture2D)
            {
                source += "    texture[uint2(texel.offset[0], texel.offset[1])] = value;\n";
            }
            else if (textureType == TextureType::Texture2DArray)
            {
                source += "    texture[uint3(texel.offset[0], texel.offset[1], texel.layer)] = value;\n";
            }
            else if (textureType == TextureType::Texture3D)
            {
                source += "    texture[uint3(texel.offset[0], texel.offset[1], texel.offset[2])] = value;\n";
            }
        }
        source += "}\n";
        // printf("Shader source:\n%s\n", source.c_str());

        ComPtr<IShaderProgram> shaderProgram;
        REQUIRE_CALL(loadComputeProgramFromSource(m_device, source, shaderProgram.writeRef()));

        ComPtr<IComputePipeline> pipeline;
        ComputePipelineDesc pipelineDesc = {};
        pipelineDesc.program = shaderProgram;
        REQUIRE_CALL(m_device->createComputePipeline(pipelineDesc, pipeline.writeRef()));
        m_writePipelines[key] = pipeline;
        return pipeline;
    }

    void writeTexelsDevice(ITextureView* textureView, span<TexelData> texels, WriteMethod writeMethod)
    {
        REQUIRE(texels.size_bytes() < m_buffer->getDesc().size);
        const TextureDesc& textureDesc = textureView->getTexture()->getDesc();
        ComPtr<IComputePipeline> pipeline = getWritePipeline(textureDesc.type, textureDesc.format, writeMethod);
        ComPtr<ICommandEncoder> commandEncoder = m_queue->createCommandEncoder();
        commandEncoder->uploadBufferData(m_buffer, 0, texels.size_bytes(), texels.data());
        auto passEncoder = commandEncoder->beginComputePass();
        auto rootObject = passEncoder->bindPipeline(pipeline);
        ShaderCursor cursor(rootObject->getEntryPoint(0));
        cursor["texture"].setBinding(textureView);
        cursor["texelData"].setBinding(m_buffer);
        cursor["texelCount"].setData(uint32_t(texels.size()));
        passEncoder->dispatchCompute(texels.size(), 1, 1);
        passEncoder->end();
        m_queue->submit(commandEncoder->finish());
        m_device->readBuffer(m_buffer, 0, texels.size_bytes(), texels.data());
    }

    ComPtr<IComputePipeline> getReadPipeline(
        TextureViewType textureViewType,
        TextureType textureType,
        Format format,
        ReadMethod readMethod
    )
    {
        ReadPipelineKey key = {textureViewType, textureType, format, readMethod};
        auto it = m_readPipelines.find(key);
        if (it != m_readPipelines.end())
            return it->second;

        const FormatInfo& info = getFormatInfo(format);
        std::string formatType = getFormatType(format);
        std::string slangTextureType = getFormatAttribute(format) + " " +
                                       (textureViewType == TextureViewType::ReadOnly ? getTextureType(textureType)
                                                                                     : getRWTextureType(textureType)) +
                                       "<" + formatType + ">";
        std::string source;
        source += getShaderPrelude();
        source += "[shader(\"compute\")]\n";
        source += "[numthreads(1,1,1)]\n";
        source += "void readTexels(\n";
        source += "    uint3 tid : SV_DispatchThreadID,\n";
        source += "    uniform " + slangTextureType + " texture,\n";
        source += "    uniform RWStructuredBuffer<TexelData> texelData,\n";
        source += "    uniform uint texelCount)\n";
        source += "{\n";
        source += "    if (tid.x > texelCount)\n";
        source += "        return;\n";
        source += "    TexelData texel = texelData[tid.x];\n";
        source += "    " + formatType + " value;\n";
        if (textureViewType == TextureViewType::ReadOnly)
        {
            if (readMethod == ReadMethod::Load)
            {
                if (textureType == TextureType::Texture1D)
                {
                    source += "    value = texture.Load(uint2(texel.offset[0], texel.mip));\n";
                }
                if (textureType == TextureType::Texture1DArray)
                {
                    source += "    value = texture.Load(uint3(texel.offset[0], texel.layer, texel.mip));\n";
                }
                else if (textureType == TextureType::Texture2D)
                {
                    source += "    value = texture.Load(uint3(texel.offset[0], texel.offset[1], texel.mip));\n";
                }
                else if (textureType == TextureType::Texture2DArray)
                {
                    source +=
                        "    value = texture.Load(uint4(texel.offset[0], texel.offset[1], texel.layer, texel.mip));\n";
                }
                else if (textureType == TextureType::Texture3D)
                {
                    source +=
                        "    value = texture.Load(uint4(texel.offset[0], texel.offset[1], texel.offset[2], "
                        "texel.mip));\n";
                }
            }
            else if (readMethod == ReadMethod::Subscript)
            {
                if (textureType == TextureType::Texture1D)
                {
                    source += "    value = texture[texel.offset[0]];\n";
                }
                if (textureType == TextureType::Texture1DArray)
                {
                    source += "    value = texture[uint2(texel.offset[0], texel.layer)];\n";
                }
                else if (textureType == TextureType::Texture2D)
                {
                    source += "    value = texture[uint2(texel.offset[0], texel.offset[1])];\n";
                }
                else if (textureType == TextureType::Texture2DArray)
                {
                    source += "    value = texture[uint3(texel.offset[0], texel.offset[1], texel.layer)];\n";
                }
                else if (textureType == TextureType::Texture3D)
                {
                    source += "    value = texture[uint3(texel.offset[0], texel.offset[1], texel.offset[2])];\n";
                }
            }
        }
        else if (textureViewType == TextureViewType::ReadWrite)
        {
            if (readMethod == ReadMethod::Load)
            {
                if (textureType == TextureType::Texture1D)
                {
                    source += "    value = texture.Load(texel.offset[0]);\n";
                }
                if (textureType == TextureType::Texture1DArray)
                {
                    source += "    value = texture.Load(uint2(texel.offset[0], texel.layer));\n";
                }
                else if (textureType == TextureType::Texture2D)
                {
                    source += "    value = texture.Load(uint2(texel.offset[0], texel.offset[1]));\n";
                }
                else if (textureType == TextureType::Texture2DArray)
                {
                    source += "    value = texture.Load(uint3(texel.offset[0], texel.offset[1], texel.layer));\n";
                }
                else if (textureType == TextureType::Texture3D)
                {
                    source += "    value = texture.Load(uint3(texel.offset[0], texel.offset[1], texel.offset[2]));\n";
                }
            }
            else if (readMethod == ReadMethod::Subscript)
            {
                if (textureType == TextureType::Texture1D)
                {
                    source += "    value = texture[texel.offset[0]];\n";
                }
                if (textureType == TextureType::Texture1DArray)
                {
                    source += "    value = texture[uint2(texel.offset[0], texel.layer)];\n";
                }
                else if (textureType == TextureType::Texture2D)
                {
                    source += "    value = texture[uint2(texel.offset[0], texel.offset[1])];\n";
                }
                else if (textureType == TextureType::Texture2DArray)
                {
                    source += "    value = texture[uint3(texel.offset[0], texel.offset[1], texel.layer)];\n";
                }
                else if (textureType == TextureType::Texture3D)
                {
                    source += "    value = texture[uint3(texel.offset[0], texel.offset[1], texel.offset[2])];\n";
                }
            }
        }
        if (info.channelCount == 1)
        {
            source += "    texel.values[0] = asuint(value);\n";
        }
        else
        {
            for (uint32_t i = 0; i < info.channelCount; ++i)
            {
                source += "    texel.values[" + std::to_string(i) + "] = asuint(value[" + std::to_string(i) + "]);\n";
            }
        }
        source += "    texelData[tid.x] = texel;\n";
        source += "}\n";
        // printf("Shader source:\n%s\n", source.c_str());

        ComPtr<IShaderProgram> shaderProgram;
        REQUIRE_CALL(loadComputeProgramFromSource(m_device, source, shaderProgram.writeRef()));

        ComPtr<IComputePipeline> pipeline;
        ComputePipelineDesc pipelineDesc = {};
        pipelineDesc.program = shaderProgram;
        REQUIRE_CALL(m_device->createComputePipeline(pipelineDesc, pipeline.writeRef()));
        m_readPipelines[key] = pipeline;
        return pipeline;
    };

    void readTexelsDevice(
        TextureViewType textureViewType,
        ITextureView* textureView,
        span<TexelData> texels,
        ReadMethod readMethod
    )
    {
        REQUIRE(texels.size_bytes() < m_buffer->getDesc().size);
        const TextureDesc& textureDesc = textureView->getTexture()->getDesc();
        ComPtr<IComputePipeline> pipeline =
            getReadPipeline(textureViewType, textureDesc.type, textureDesc.format, readMethod);
        ComPtr<ICommandEncoder> commandEncoder = m_queue->createCommandEncoder();
        commandEncoder->uploadBufferData(m_buffer, 0, texels.size_bytes(), texels.data());
        auto passEncoder = commandEncoder->beginComputePass();
        auto rootObject = passEncoder->bindPipeline(pipeline);
        ShaderCursor cursor(rootObject->getEntryPoint(0));
        cursor["texture"].setBinding(textureView);
        cursor["texelData"].setBinding(m_buffer);
        cursor["texelCount"].setData(uint32_t(texels.size()));
        passEncoder->dispatchCompute(texels.size(), 1, 1);
        passEncoder->end();
        m_queue->submit(commandEncoder->finish());
        m_device->readBuffer(m_buffer, 0, texels.size_bytes(), texels.data());
    }
};

static std::vector<TexelData> generateTexelData(ITextureView* textureView)
{
    const TextureDesc& textureDesc = textureView->getTexture()->getDesc();
    const SubresourceRange& srRange = textureView->getDesc().subresourceRange;

    const FormatInfo& info = getFormatInfo(textureDesc.format);

    std::vector<TexelData> texels;

    uint32_t subresourceCount = srRange.layerCount * srRange.mipCount;
    uint32_t subresourceIndex = 0;

    for (uint32_t layer = 0; layer < srRange.layerCount; ++layer)
    {
        for (uint32_t mip = 0; mip < srRange.mipCount; ++mip)
        {
            TexelData texel;
            texel.layer = layer;
            texel.mip = mip;
            texel.offset[0] = 0;
            texel.offset[1] = 0;
            texel.offset[2] = 0;

            if (info.kind == FormatKind::Integer && info.isSigned)
            {
                texel.ints[0] = -10 - subresourceIndex;
                texel.ints[1] = -1;
                texel.ints[2] = 1;
                texel.ints[3] = 2;
            }
            else if (info.kind == FormatKind::Integer && !info.isSigned)
            {
                texel.uints[0] = 10 + subresourceIndex;
                texel.uints[1] = 1;
                texel.uints[2] = 2;
                texel.uints[3] = 3;
            }
            else if (info.kind == FormatKind::Normalized)
            {
                texel.floats[0] = float(subresourceIndex + 1) / subresourceCount;
                texel.floats[1] = 0.5f;
                texel.floats[2] = 0.75f;
                texel.floats[3] = 1.f;
            }
            else if (info.kind == FormatKind::Float)
            {
                texel.floats[0] = 10.f + subresourceIndex;
                texel.floats[1] = 20.f;
                texel.floats[2] = 30.f;
                texel.floats[3] = 40.f;
            }
            else
            {
                FAIL("Unsupported format");
            }

            texels.push_back(texel);

            uint32_t absMip = srRange.mip + mip;
            uint32_t mipWidth = std::max(1u, textureDesc.size.width >> absMip);
            uint32_t mipHeight = std::max(1u, textureDesc.size.height >> absMip);
            uint32_t mipDepth = std::max(1u, textureDesc.size.depth >> absMip);
            if (mipWidth == 1 && mipHeight == 1 && mipDepth == 1)
                continue;

            texel.offset[0] = mipWidth - 1;
            texel.offset[1] = mipHeight - 1;
            texel.offset[2] = mipDepth - 1;

            if (info.kind == FormatKind::Integer && info.isSigned)
            {
                texel.ints[0] = -11 - subresourceIndex;
                texel.ints[1] = 2;
                texel.ints[2] = 1;
                texel.ints[3] = -1;
            }
            else if (info.kind == FormatKind::Integer && !info.isSigned)
            {
                texel.uints[0] = 11 + subresourceIndex;
                texel.uints[1] = 3;
                texel.uints[2] = 2;
                texel.uints[3] = 1;
            }
            else if (info.kind == FormatKind::Normalized)
            {
                texel.floats[0] = float(subresourceIndex + 1) / subresourceCount;
                texel.floats[1] = 1.f;
                texel.floats[2] = 0.75f;
                texel.floats[3] = 0.5f;
            }
            else if (info.kind == FormatKind::Float)
            {
                texel.floats[0] = 11.f + subresourceIndex;
                texel.floats[1] = 40.f;
                texel.floats[2] = 30.f;
                texel.floats[3] = 20.f;
            }

            texels.push_back(texel);

            subresourceIndex += 1;
        }
    }

    return texels;
}

// Test host write and read-back infrastructure.
GPU_TEST_CASE("texture-view-host-write-read", D3D12 | Vulkan | CUDA | Metal)
{
    TextureViewTest test(device);

    TextureTestOptions options(device);
    options.addVariants(
        TTShape::D1 | TTShape::D2 | TTShape::D3,
        TTArray::Both,        // non-array/array
        TTMip::Both,          // with/without mips
        TTMS::Off,            // without multisampling
        TTPowerOf2::Both,     // test both power-of-2 and non-power-of-2 sizes where possible
        TTFmtCompressed::Off, // without compressed formats
        TTFmtDepth::Off       // without depth formats
#if TEST_SPECIFIC_FORMATS
        ,
        kFormats
#endif
    );

    runTextureTest(
        options,
        [&](TextureTestContext* c)
        {
            const TextureDesc& desc = c->getTexture()->getDesc();
            if (shouldSkipFormat(desc.format))
                return;

            ComPtr<ITextureView> textureView = c->getTexture()->getDefaultView();

            // Generate reference texel data
            std::vector<TexelData> refTexels = generateTexelData(textureView);

            // Write reference texel data
            test.writeTexelsHost(textureView, refTexels);

            // Read back the texel data and compare
            std::vector<TexelData> readTexels = refTexels;
            clearTexelDataValues(readTexels);
            test.readTexelsHost(textureView, readTexels);
            compareTexelData(desc.format, refTexels, readTexels);
        }
    );

    device->getQueue(QueueType::Graphics)->waitOnHost();
}

// Test shader side .Load() on read-only textures with views including all layers and mips.
GPU_TEST_CASE("texture-view-load-ro-all-layers-all-mips", D3D12 | Vulkan | CUDA | Metal)
{
    TextureViewTest test(device);

    TextureTestOptions options(device);
    options.addVariants(
        TTShape::D1 | TTShape::D2 | TTShape::D3,
        TTArray::Both,        // with/without arrays
        TTMip::Both,          // with/without mips
        TTMS::Off,            // without multisampling
        TTPowerOf2::Off,      // without power-of-two sizes (to limit the number of test cases)
        TTFmtCompressed::Off, // without compressed formats
        TTFmtDepth::Off,      // without depth formats
        TextureUsage::ShaderResource
#if TEST_SPECIFIC_FORMATS
        ,
        kFormats
#endif
    );

    runTextureTest(
        options,
        [&](TextureTestContext* c)
        {
            const TextureDesc& desc = c->getTexture()->getDesc();
            if (shouldSkipFormat(desc.format))
                return;

            // CUDA does not support loads from 1D textures (limitation in PTX ISA).
            if (device->getDeviceType() == DeviceType::CUDA && (desc.type == TextureType::Texture1D))
                return;

            ComPtr<ITextureView> textureView = c->getTexture()->getDefaultView();

            // Generate reference texel data
            std::vector<TexelData> refTexels = generateTexelData(textureView);

            // Write reference texel data
            test.writeTexelsHost(textureView, refTexels);

            // Read back the texel data in shader using .Load() and compare
            std::vector<TexelData> readTexels = refTexels;
            clearTexelDataValues(readTexels);
            test.readTexelsDevice(TextureViewType::ReadOnly, textureView, readTexels, ReadMethod::Load);
            compareTexelData(desc.format, refTexels, readTexels);
        }
    );

    device->getQueue(QueueType::Graphics)->waitOnHost();
}

// Test shader side .Load() and subscript load operator on read-only textures with views including all layers and a
// single mip.
GPU_TEST_CASE("texture-view-load-ro-all-layers-single-mip", D3D12 | Vulkan | CUDA | Metal)
{
    TextureViewTest test(device);

    TextureTestOptions options(device);
    options.addVariants(
        TTShape::D1 | TTShape::D2 | TTShape::D3,
        TTArray::Both,        // with/without arrays
        TTMip::Both,          // with/without mips
        TTMS::Off,            // without multisampling
        TTPowerOf2::Off,      // without power-of-two sizes (to limit the number of test cases)
        TTFmtCompressed::Off, // without compressed formats
        TTFmtDepth::Off,      // without depth formats
        TextureUsage::ShaderResource
#if TEST_SPECIFIC_FORMATS
        ,
        kFormats
#endif
    );

    runTextureTest(
        options,
        [&](TextureTestContext* c)
        {
            const TextureDesc& desc = c->getTexture()->getDesc();
            if (shouldSkipFormat(desc.format))
                return;

            // CUDA does not support loads from 1D textures (limitation in PTX ISA).
            if (device->getDeviceType() == DeviceType::CUDA && (desc.type == TextureType::Texture1D))
                return;

            for (uint32_t mip = 0; mip < desc.mipCount; ++mip)
            {
                TextureViewDesc viewDesc = {};
                viewDesc.subresourceRange.mip = mip;
                viewDesc.subresourceRange.mipCount = 1;
                ComPtr<ITextureView> textureView = c->getTexture()->createView(viewDesc);

                // Generate reference texel data
                std::vector<TexelData> refTexels = generateTexelData(textureView);

                // Write reference texel data
                test.writeTexelsHost(textureView, refTexels);

                // Read back the texel data in shader using .Load() and compare
                {
                    std::vector<TexelData> readTexels = refTexels;
                    clearTexelDataValues(readTexels);
                    test.readTexelsDevice(TextureViewType::ReadOnly, textureView, readTexels, ReadMethod::Load);
                    compareTexelData(desc.format, refTexels, readTexels);
                }

                // Read back the texel data in shader using .subscript() and compare
                {
                    std::vector<TexelData> readTexels = refTexels;
                    clearTexelDataValues(readTexels);
                    test.readTexelsDevice(TextureViewType::ReadOnly, textureView, readTexels, ReadMethod::Subscript);
                    compareTexelData(desc.format, refTexels, readTexels);
                }
            }
        }
    );

    device->getQueue(QueueType::Graphics)->waitOnHost();
}

// Test shader side .Load() and subscript load operator on read-write textures with views including all layers and a
// single mip.
GPU_TEST_CASE("texture-view-load-rw-all-layers-single-mip", D3D12 | Vulkan | CUDA | Metal)
{
    TextureViewTest test(device);

    TextureTestOptions options(device);
    options.addVariants(
        TTShape::D1 | TTShape::D2 | TTShape::D3,
        TTArray::Both,        // with/without arrays
        TTMip::Both,          // with/without mips
        TTMS::Off,            // without multisampling
        TTPowerOf2::Off,      // without power-of-two sizes (to limit the number of test cases)
        TTFmtCompressed::Off, // without compressed formats
        TTFmtDepth::Off,      // without depth formats
        TextureUsage::ShaderResource | TextureUsage::UnorderedAccess
#if TEST_SPECIFIC_FORMATS
        ,
        kFormats
#endif
    );

    runTextureTest(
        options,
        [&](TextureTestContext* c)
        {
            const TextureDesc& desc = c->getTexture()->getDesc();
            if (shouldSkipFormat(desc.format))
                return;

            // CUDA does not support loads from surfaces that need format conversion (limitation in PTX ISA).
            if (device->getDeviceType() == DeviceType::CUDA && needsFormatConversion(desc.format))
                return;

            for (uint32_t mip = 0; mip < desc.mipCount; ++mip)
            {
                TextureViewDesc viewDesc = {};
                viewDesc.subresourceRange.mip = mip;
                viewDesc.subresourceRange.mipCount = 1;
                ComPtr<ITextureView> textureView = c->getTexture()->createView(viewDesc);

                // Generate reference texel data
                std::vector<TexelData> refTexels = generateTexelData(textureView);

                // Write reference texel data
                test.writeTexelsHost(textureView, refTexels);

                // Read back the texel data in shader using .Load() and compare
                {
                    std::vector<TexelData> readTexels = refTexels;
                    clearTexelDataValues(readTexels);
                    test.readTexelsDevice(TextureViewType::ReadWrite, textureView, readTexels, ReadMethod::Load);
                    compareTexelData(desc.format, refTexels, readTexels);
                }

                // Read back the texel data in shader using .subscript() and compare
                {
                    std::vector<TexelData> readTexels = refTexels;
                    clearTexelDataValues(readTexels);
                    test.readTexelsDevice(TextureViewType::ReadWrite, textureView, readTexels, ReadMethod::Subscript);
                    compareTexelData(desc.format, refTexels, readTexels);
                }
            }
        }
    );

    device->getQueue(QueueType::Graphics)->waitOnHost();
}

// Test shader side .Load() and subscript load operator on read-only textures with views including a single layer and
// mip.
GPU_TEST_CASE("texture-view-load-ro-single", D3D12 | Vulkan | CUDA | Metal)
{
    TextureViewTest test(device);

    TextureTestOptions options(device);
    options.addVariants(
        TTShape::D1 | TTShape::D2 | TTShape::D3,
        TTArray::Both,        // with/without arrays
        TTMip::Both,          // with/without mips
        TTMS::Off,            // without multisampling
        TTPowerOf2::Off,      // without power-of-two sizes (to limit the number of test cases)
        TTFmtCompressed::Off, // without compressed formats
        TTFmtDepth::Off,      // without depth formats
        TextureUsage::ShaderResource
#if TEST_SPECIFIC_FORMATS
        ,
        kFormats
#endif
    );

    runTextureTest(
        options,
        [&](TextureTestContext* c)
        {
            const TextureDesc& desc = c->getTexture()->getDesc();
            if (shouldSkipFormat(desc.format))
                return;

            // CUDA does not support loads from 1D textures (limitation in PTX ISA).
            if (device->getDeviceType() == DeviceType::CUDA && (desc.type == TextureType::Texture1D))
                return;

            for (uint32_t layer = 0; layer < desc.arrayLength; ++layer)
            {
                for (uint32_t mip = 0; mip < desc.mipCount; ++mip)
                {
                    TextureViewDesc viewDesc = {};
                    viewDesc.subresourceRange.layer = layer;
                    viewDesc.subresourceRange.layerCount = 1;
                    viewDesc.subresourceRange.mip = mip;
                    viewDesc.subresourceRange.mipCount = 1;
                    ComPtr<ITextureView> textureView = c->getTexture()->createView(viewDesc);

                    // Generate reference texel data
                    std::vector<TexelData> refTexels = generateTexelData(textureView);

                    // Write reference texel data
                    test.writeTexelsHost(textureView, refTexels);

                    // Read back the texel data in shader using .Load() and compare
                    {
                        std::vector<TexelData> readTexels = refTexels;
                        clearTexelDataValues(readTexels);
                        test.readTexelsDevice(TextureViewType::ReadOnly, textureView, readTexels, ReadMethod::Load);
                        compareTexelData(desc.format, refTexels, readTexels);
                    }

                    // Read back the texel data in shader using .subscript() and compare
                    {
                        std::vector<TexelData> readTexels = refTexels;
                        clearTexelDataValues(readTexels);
                        test.readTexelsDevice(
                            TextureViewType::ReadOnly,
                            textureView,
                            readTexels,
                            ReadMethod::Subscript
                        );
                        compareTexelData(desc.format, refTexels, readTexels);
                    }
                }
            }
        }
    );

    device->getQueue(QueueType::Graphics)->waitOnHost();
}

// Test shader side .Load() and subscript load operator on read-write textures with views including a single layer and
// mip.
GPU_TEST_CASE("texture-view-load-rw-single", D3D12 | Vulkan | CUDA | Metal)
{
    TextureViewTest test(device);

    TextureTestOptions options(device);
    options.addVariants(
        TTShape::D1 | TTShape::D2 | TTShape::D3,
        TTArray::Both,        // with/without arrays
        TTMip::Both,          // with/without mips
        TTMS::Off,            // without multisampling
        TTPowerOf2::Off,      // without power-of-two sizes (to limit the number of test cases)
        TTFmtCompressed::Off, // without compressed formats
        TTFmtDepth::Off,      // without depth formats
        TextureUsage::ShaderResource | TextureUsage::UnorderedAccess,
        TextureInitMode::None
#if TEST_SPECIFIC_FORMATS
        ,
        kFormats
#endif
    );

    runTextureTest(
        options,
        [&](TextureTestContext* c)
        {
            const TextureDesc& desc = c->getTexture()->getDesc();
            if (shouldSkipFormat(desc.format))
                return;

            // CUDA does not support loads from surfaces that need format conversion (limitation in PTX ISA).
            if (device->getDeviceType() == DeviceType::CUDA && needsFormatConversion(desc.format))
                return;

            for (uint32_t layer = 0; layer < desc.arrayLength; ++layer)
            {
                for (uint32_t mip = 0; mip < desc.mipCount; ++mip)
                {
                    TextureViewDesc viewDesc = {};
                    viewDesc.subresourceRange.layer = layer;
                    viewDesc.subresourceRange.layerCount = 1;
                    viewDesc.subresourceRange.mip = mip;
                    viewDesc.subresourceRange.mipCount = 1;
                    ComPtr<ITextureView> textureView = c->getTexture()->createView(viewDesc);

                    // Generate reference texel data
                    std::vector<TexelData> refTexels = generateTexelData(textureView);

                    // Write reference texel data
                    test.writeTexelsHost(textureView, refTexels);

                    // Read back the texel data in shader using .Load() and compare
                    {
                        std::vector<TexelData> readTexels = refTexels;
                        clearTexelDataValues(readTexels);
                        test.readTexelsDevice(TextureViewType::ReadWrite, textureView, readTexels, ReadMethod::Load);
                        compareTexelData(desc.format, refTexels, readTexels);
                    }

                    // Read back the texel data in shader using .subscript() and compare
                    {
                        std::vector<TexelData> readTexels = refTexels;
                        clearTexelDataValues(readTexels);
                        test.readTexelsDevice(
                            TextureViewType::ReadWrite,
                            textureView,
                            readTexels,
                            ReadMethod::Subscript
                        );
                        compareTexelData(desc.format, refTexels, readTexels);
                    }
                }
            }
        }
    );

    device->getQueue(QueueType::Graphics)->waitOnHost();
}

// Test shader side .Store() and subscript store operator on read-write textures with views including all layers and a
// single mip.
GPU_TEST_CASE("texture-view-store-all-layers-single-mip", D3D12 | Vulkan | CUDA | Metal)
{
    TextureViewTest test(device);

    TextureTestOptions options(device);
    options.addVariants(
        TTShape::D1 | TTShape::D2 | TTShape::D3,
        TTArray::Both,        // with/without arrays
        TTMip::Both,          // with/without mips
        TTMS::Off,            // without multisampling
        TTPowerOf2::Off,      // without power-of-two sizes (to limit the number of test cases)
        TTFmtCompressed::Off, // without compressed formats
        TTFmtDepth::Off,      // without depth formats
        TextureUsage::UnorderedAccess,
        TextureInitMode::None
#if TEST_SPECIFIC_FORMATS
        ,
        kFormats
#endif
    );

    runTextureTest(
        options,
        [&](TextureTestContext* c)
        {
            const TextureDesc& desc = c->getTexture()->getDesc();
            if (shouldSkipFormat(desc.format))
                return;

            // CUDA does not support stores to surfaces that need format conversion (limitation in PTX ISA).
            if (device->getDeviceType() == DeviceType::CUDA && needsFormatConversion(desc.format))
                return;

            for (uint32_t mip = 0; mip < desc.mipCount; ++mip)
            {
                TextureViewDesc viewDesc = {};
                viewDesc.subresourceRange.mip = mip;
                viewDesc.subresourceRange.mipCount = 1;
                ComPtr<ITextureView> textureView = c->getTexture()->createView(viewDesc);

                // Generate reference texel data
                std::vector<TexelData> refTexels = generateTexelData(textureView);

                // Write the texel data in shader using .Store() and compare
                {
                    test.writeTexelsDevice(textureView, refTexels, WriteMethod::Store);
                    std::vector<TexelData> readTexels = refTexels;
                    clearTexelDataValues(readTexels);
                    test.readTexelsHost(textureView, readTexels);
                    compareTexelData(desc.format, refTexels, readTexels);
                }

                // Clear texels.
                {
                    std::vector<TexelData> clearTexels = refTexels;
                    clearTexelDataValues(clearTexels);
                    test.writeTexelsHost(textureView, clearTexels);
                }

                // Write the texel data in shader using .Store() and compare
                {
                    test.writeTexelsDevice(textureView, refTexels, WriteMethod::Subscript);
                    std::vector<TexelData> readTexels = refTexels;
                    clearTexelDataValues(readTexels);
                    test.readTexelsHost(textureView, readTexels);
                    compareTexelData(desc.format, refTexels, readTexels);
                }
            }
        }
    );

    device->getQueue(QueueType::Graphics)->waitOnHost();
}

// Test shader side .Store() and subscript store operator on read-write textures with views including a single layer and
// mip.
GPU_TEST_CASE("texture-view-store-single", D3D12 | Vulkan | CUDA | Metal)
{
    TextureViewTest test(device);

    TextureTestOptions options(device);
    options.addVariants(
        TTShape::D1 | TTShape::D2 | TTShape::D3,
        TTArray::Both,        // with/without arrays
        TTMip::Both,          // with/without mips
        TTMS::Off,            // without multisampling
        TTPowerOf2::Off,      // without power-of-two sizes (to limit the number of test cases)
        TTFmtCompressed::Off, // without compressed formats
        TTFmtDepth::Off,      // without depth formats
        TextureUsage::UnorderedAccess,
        TextureInitMode::None
#if TEST_SPECIFIC_FORMATS
        ,
        kFormats
#endif
    );

    runTextureTest(
        options,
        [&](TextureTestContext* c)
        {
            const TextureDesc& desc = c->getTexture()->getDesc();
            if (shouldSkipFormat(desc.format))
                return;

            // CUDA does not support stores to surfaces that need format conversion (limitation in PTX ISA).
            if (device->getDeviceType() == DeviceType::CUDA && needsFormatConversion(desc.format))
                return;
            // CUDA does not support creating a surface from a subset of layers.
            // TODO: We should check for that in the validation layer.
            if (device->getDeviceType() == DeviceType::CUDA &&
                (desc.type == TextureType::Texture1DArray || desc.type == TextureType::Texture2DArray) &&
                desc.arrayLength > 1)
                return;

            for (uint32_t layer = 0; layer < desc.arrayLength; ++layer)
            {
                for (uint32_t mip = 0; mip < desc.mipCount; ++mip)
                {
                    TextureViewDesc viewDesc = {};
                    viewDesc.subresourceRange.layer = layer;
                    viewDesc.subresourceRange.layerCount = 1;
                    viewDesc.subresourceRange.mip = mip;
                    viewDesc.subresourceRange.mipCount = 1;
                    ComPtr<ITextureView> textureView = c->getTexture()->createView(viewDesc);

                    // Generate reference texel data
                    std::vector<TexelData> refTexels = generateTexelData(textureView);

                    // Write the texel data in shader using .Store() and compare
                    {
                        test.writeTexelsDevice(textureView, refTexels, WriteMethod::Store);
                        std::vector<TexelData> readTexels = refTexels;
                        clearTexelDataValues(readTexels);
                        test.readTexelsHost(textureView, readTexels);
                        compareTexelData(desc.format, refTexels, readTexels);
                    }

                    // Clear texels.
                    {
                        std::vector<TexelData> clearTexels = refTexels;
                        clearTexelDataValues(clearTexels);
                        test.writeTexelsHost(textureView, clearTexels);
                    }

                    // Write the texel data in shader using .Store() and compare
                    {
                        test.writeTexelsDevice(textureView, refTexels, WriteMethod::Subscript);
                        std::vector<TexelData> readTexels = refTexels;
                        clearTexelDataValues(readTexels);
                        test.readTexelsHost(textureView, readTexels);
                        compareTexelData(desc.format, refTexels, readTexels);
                    }
                }
            }
        }
    );

    device->getQueue(QueueType::Graphics)->waitOnHost();
}
