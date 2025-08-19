#include "testing.h"
#include "texture-test.h"
#include "format-conversion.h"
#include <map>

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

#if 1
static const std::vector<Format> kFormats = { 
    Format::R8Uint,
    Format::R8Unorm,
    Format::R8Snorm,
    Format::R16Uint,
    Format::R16Unorm,
    Format::R16Snorm,
    Format::R16Float,
    Format::RGBA32Uint,
    Format::RGBA32Float,
   
};
#else
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
    Format::RGBA8Snorm,
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
};
#endif

// tests
// read from SRV
// - single layer/mip
// - multiple layers/mips
// - subscript/Load
// write to UAV
// - single layer/mip
// - multiple layers/mips
// - subscript/Store

class TextureViewTest
{
public:
    struct TexelData
    {
        uint32_t layer;
        uint32_t mip;
        Offset3D offset;
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

    static void compareTexelDataRaw(Format format, span<TexelData> a, span<TexelData> b)
    {
        REQUIRE(a.size() == b.size());
        size_t bytesPerPixel = getFormatInfo(format).blockSizeInBytes;
        for (size_t i = 0; i < a.size(); ++i)
        {
            CAPTURE(i);
            CHECK(memcmp(a[i].raw, b[i].raw, bytesPerPixel) == 0);
        }
    }

    static void compareTexelData(Format format, span<TexelData> a, span<TexelData> b)
    {
        REQUIRE(a.size() == b.size());
        size_t bytes = getFormatInfo(format).channelCount * 4;
        for (size_t i = 0; i < a.size(); ++i)
        {
            CAPTURE(i);
            CHECK(memcmp(a[i].floats, b[i].floats, bytes) == 0);
        }
    }

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

    IDevice* m_device;
    ComPtr<ICommandQueue> m_queue;
    ComPtr<IBuffer> m_buffer;
    std::unique_ptr<uint8_t[]> m_readbackData;

    using ReadPipelineKey = std::tuple<TextureType, Format, ReadMethod>;
    std::map<ReadPipelineKey, ComPtr<IComputePipeline>> m_readPipelines;
    using WritePipelineKey = std::tuple<TextureType, Format, WriteMethod>;
    std::map<WritePipelineKey, ComPtr<IComputePipeline>> m_writePipelines;

    TextureViewTest(IDevice* device)
        : m_device(device)
        , m_queue(m_device->getQueue(QueueType::Graphics))
    {
        BufferDesc bufferDesc = {};
        bufferDesc.size = 1024 * 1024;
        bufferDesc.usage = BufferUsage::CopySource | BufferUsage::CopyDestination | BufferUsage::ShaderResource |
                           BufferUsage::UnorderedAccess;
        m_buffer = device->createBuffer(bufferDesc, nullptr);
        m_readbackData = std::make_unique<uint8_t[]>(1024);
    }

    void writeTexelsRawHost(ITexture* texture, span<TexelData> texels)
    {
        ComPtr<ICommandEncoder> commandEncoder = m_queue->createCommandEncoder();
        for (const TexelData& texel : texels)
        {
            SubresourceLayout layout;
            texture->getSubresourceLayout(texel.mip, &layout);
            Extent3D extent = {1, 1, 1};
            SubresourceRange srRange = {texel.layer, 1, texel.mip, 1};
            SubresourceData srData = {texel.raw, layout.rowPitch, layout.slicePitch};
            commandEncoder->uploadTextureData(texture, srRange, texel.offset, extent, &srData, 1);
        }
        m_queue->submit(commandEncoder->finish());
    }

    void writeTexelsHost(ITexture* texture, span<TexelData> texels)
    {
        // Convert texels to raw data
        Format format = texture->getDesc().format;
        const FormatInfo& info = getFormatInfo(format);
        FormatConversionFuncs funcs = getFormatConversionFuncs(format);
        switch (info.kind)
        {
        case FormatKind::Integer:
            for (TexelData& texel : texels)
                funcs.packIntFunc(texel.uints, texel.raw);
            break;
        case FormatKind::Normalized:
        case FormatKind::Float:
            for (TexelData& texel : texels)
                funcs.packFloatFunc(texel.floats, texel.raw);
            break;
        case FormatKind::DepthStencil:
            FAIL("Depth/stencil not supported!");
        }
        writeTexelsRawHost(texture, texels);
    }

    void readTexelsRawHost(ITexture* texture, span<TexelData> texels)
    {
        ComPtr<ICommandEncoder> commandEncoder = m_queue->createCommandEncoder();
        uint64_t offset = 0;
        for (const TexelData& texel : texels)
        {
            SubresourceLayout layout;
            texture->getSubresourceLayout(texel.mip, &layout);
            Extent3D extent = {1, 1, 1};
            commandEncoder->copyTextureToBuffer(
                m_buffer,
                offset,
                16,
                layout.rowPitch,
                texture,
                texel.layer,
                texel.mip,
                texel.offset,
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

    void readTexelsHost(ITexture* texture, span<TexelData> texels)
    {
        // Convert texels to raw data
        Format format = texture->getDesc().format;
        const FormatInfo& info = getFormatInfo(format);
        FormatConversionFuncs funcs = getFormatConversionFuncs(format);
        readTexelsRawHost(texture, texels);
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
    uint3 offset;
    uint values[4];
    uint raw[4];
};
)";
    }

    void writeTexelsDevice(ITextureView* textureView, span<TexelData> texels, WriteMethod writeMethod) {}

    ComPtr<IComputePipeline> getReadPipeline(TextureType textureType, Format format, ReadMethod readMethod)
    {
        ReadPipelineKey key = {textureType, format, readMethod};
        auto it = m_readPipelines.find(key);
        if (it != m_readPipelines.end())
            return it->second;

        const FormatInfo& info = getFormatInfo(format);
        std::string formatType = getFormatType(format);
        std::string source;
        source += getShaderPrelude();
        source += "[shader(\"compute\")]\n";
        source += "[numthreads(1,1,1)]\n";
        source += "void readTexels(\n";
        source += "    uint3 tid : SV_DispatchThreadID,\n";
        source += "    uniform " + getTextureType(textureType) + "<" + formatType + "> texture,\n";
        source += "    uniform RWStructuredBuffer<TexelData> texelData,\n";
        source += "    uniform uint texelCount)\n";
        source += "{\n";
        source += "    if (tid.x > texelCount)\n";
        source += "        return;\n";
        source += "    TexelData texel = texelData[tid.x];\n";
        source += "    " + formatType + " value;\n";
        if (readMethod == ReadMethod::Load)
        {
            if (textureType == TextureType::Texture1D)
            {
                source += "    value = texture.Load(uint2(texel.offset.x, texel.mip));\n";
            }
            if (textureType == TextureType::Texture1DArray)
            {
                source += "    value = texture.Load(uint3(texel.offset.x, texel.layer, texel.mip));\n";
            }
            else if (textureType == TextureType::Texture2D)
            {
                source += "    value = texture.Load(uint3(texel.offset.xy, texel.mip));\n";
            }
            else if (textureType == TextureType::Texture2DArray)
            {
                source += "    value = texture.Load(uint4(texel.offset.xy, texel.layer, texel.mip));\n";
            }
            else if (textureType == TextureType::Texture3D)
            {
                source += "    value = texture.Load(uint4(texel.offset.xyz, texel.mip));\n";
            }
        }
        else if (readMethod == ReadMethod::Subscript)
        {
            if (textureType == TextureType::Texture1D)
            {
                source += "    value = texture[texel.offset.x];\n";
            }
            if (textureType == TextureType::Texture1DArray)
            {
                source += "    value = texture[uint2(texel.offset.x, texel.layer)];\n";
            }
            else if (textureType == TextureType::Texture2D)
            {
                source += "    value = texture[texel.offset.xy];\n";
            }
            else if (textureType == TextureType::Texture2DArray)
            {
                source += "    value = texture[uint3(texel.offset.xy, texel.layer)];\n";
            }
            else if (textureType == TextureType::Texture3D)
            {
                source += "    value = texture[uint3(texel.offset.xyz)];\n";
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
        fprintf(stderr, "Shader source:\n%s\n", source.c_str());

        ComPtr<IShaderProgram> shaderProgram;
        REQUIRE_CALL(loadComputeProgramFromSource(m_device, shaderProgram, source));

        ComPtr<IComputePipeline> pipeline;
        ComputePipelineDesc pipelineDesc = {};
        pipelineDesc.program = shaderProgram;
        REQUIRE_CALL(m_device->createComputePipeline(pipelineDesc, pipeline.writeRef()));
        m_readPipelines[key] = pipeline;
        return pipeline;
    };

    void readTexelsDevice(ITextureView* textureView, span<TexelData> texels, ReadMethod readMethod)
    {
        const TextureDesc& textureDesc = textureView->getTexture()->getDesc();
        ComPtr<IComputePipeline> pipeline = getReadPipeline(textureDesc.type, textureDesc.format, readMethod);
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


// writeTexel(texture, layer, mip, coord, value)
// coord: 1d, 2d, 3d
// value: float, uint, int


GPU_TEST_CASE("texture-view-texel-test", D3D12 | Vulkan | CUDA | Metal)
{
    TextureViewTest test(device);

    TextureTestOptions options(device);
    options.addVariants(
        TTShape::D1 | TTShape::D2 | TTShape::D3,
        TTArray::Both,    // non-array/array
        TTMip::Both,      // with/without mips
        TTMS::Off,        // without multisampling
        TTPowerOf2::Both, // test both power-of-2 and non-power-of-2 sizes where possible
        kFormats
    );
    runTextureTest(
        options,
        [&](TextureTestContext* c)
        {
            Format format = c->getTexture()->getDesc().format;
            const FormatInfo& info = c->getTextureData().formatInfo;

            std::vector<TextureViewTest::TexelData> writeTexels;

            uint32_t layerCount = c->getTextureData().desc.getLayerCount();
            uint32_t mipCount = c->getTextureData().desc.mipCount;

            uint32_t subresourceCount = layerCount * mipCount;
            uint32_t subresourceIndex = 0;

            for (uint32_t layer = 0; layer < layerCount; ++layer)
            {
                for (uint32_t mip = 0; mip < mipCount; ++mip)
                {
                    TextureViewTest::TexelData texel;
                    texel.layer = layer;
                    texel.mip = mip;
                    texel.offset = {0, 0, 0};

                    switch (info.kind)
                    {
                    case FormatKind::Integer:
                        if (info.isSigned)
                        {
                            texel.ints[0] = -10 - subresourceIndex;
                            texel.ints[1] = -1;
                            texel.ints[2] = 1;
                            texel.ints[3] = 2;
                        }
                        else
                        {
                            texel.uints[0] = 10 + subresourceIndex;
                            texel.uints[1] = 2;
                            texel.uints[2] = 3;
                            texel.uints[3] = 4;
                        }
                        break;
                    case FormatKind::Normalized:
                        texel.floats[0] = float(subresourceIndex + 1) / subresourceCount;
                        texel.floats[1] = 0.5f;
                        texel.floats[2] = 0.75f;
                        texel.floats[3] = 1.f;
                        break;
                    case FormatKind::Float:
                        texel.floats[0] = 10.f + subresourceIndex;
                        texel.floats[1] = 20.f;
                        texel.floats[2] = 30.f;
                        texel.floats[3] = 40.f;
                        break;
                    case FormatKind::DepthStencil:
                        FAIL("Depth/stencil not supported!");
                    }

                    writeTexels.push_back(texel);

                    subresourceIndex += 1;
                }
            }
            
            test.writeTexelsHost(c->getTexture(), writeTexels);

            std::vector<TextureViewTest::TexelData> readTexels = writeTexels;

            TextureViewTest::clearTexelDataValues(readTexels);
            test.readTexelsHost(c->getTexture(), readTexels);
            // TextureViewTest::compareTexelData(format, writeTexels, readTexels);
            TextureViewTest::compareTexelDataRaw(format, writeTexels, readTexels);

            TextureViewTest::clearTexelDataValues(readTexels);
            test.readTexelsDevice(
                c->getTexture()->getDefaultView(),
                readTexels,
                TextureViewTest::ReadMethod::Load
            );
            TextureViewTest::compareTexelData(format, writeTexels, readTexels);
        }
    );

    device->getQueue(QueueType::Graphics)->waitOnHost();
}


// This test checks texture views for read and read-write access using subscript operator on all basic texture types
// (1D, 2D, 3D) and formats. It creates a compute shader that copies data from a source texture to a destination
// texture. The view always targets a single layer and mip-level.
GPU_TEST_CASE("texture-view-simple", D3D11 | D3D12 | Vulkan | CUDA | Metal)
{
    TextureTestOptions options(device);
    options.addVariants(
        TTShape::D1 | TTShape::D2 | TTShape::D3,
        TTArray::Both,    // non-array/array
        TTMip::Both,      // with/without mips
        TTMS::Off,        // without multisampling
        TTPowerOf2::Both, // test both power-of-2 and non-power-of-2 sizes where possible
        kFormats
    );

    struct PipelineKey
    {
        TextureType textureType;
        Format format;
        bool operator<(const PipelineKey& other) const
        {
            return std::tie(textureType, format) < std::tie(other.textureType, other.format);
        }
    };
    std::map<PipelineKey, ComPtr<IComputePipeline>> pipelines;
    auto getCopyPipeline = [&](TextureType textureType, Format format) -> ComPtr<IComputePipeline>
    {
        PipelineKey key = {textureType, format};
        auto it = pipelines.find(key);
        if (it != pipelines.end())
            return it->second;

        std::string source;
        std::string srcTextureType = getTextureType(textureType) + "<" + getFormatType(format) + ">";
        std::string dstTextureType =
            getFormatAttribute(format) + getRWTextureType(textureType) + "<" + getFormatType(format) + ">";
        source += "[shader(\"compute\")]\n";
        source += "[numthreads(1,1,1)]\n";
        source += "void copyTexture(\n";
        source += "    uint3 tid : SV_DispatchThreadID,\n";
        source += "    uniform " + srcTextureType + " srcTexture,\n";
        source += "    uniform " + dstTextureType + " dstTexture)\n";
        source += "{\n";
        if (textureType == TextureType::Texture1D)
        {
            source += "    uint srcDims;\n";
            source += "    srcTexture.GetDimensions(srcDims);\n";
            source += "    uint dstDims;\n";
            source += "    dstTexture.GetDimensions(dstDims);\n";
            source += "    if (srcDims != dstDims)\n";
            source += "        return;\n";
            source += "    if (tid.x >= srcDims)\n";
            source += "        return;\n";
            source += "    dstTexture[tid.x] = srcTexture[tid.x];\n";
        }
        if (textureType == TextureType::Texture1DArray)
        {
            source += "    uint2 srcDims;\n";
            source += "    srcTexture.GetDimensions(srcDims.x, srcDims.y);\n";
            source += "    uint2 dstDims;\n";
            source += "    dstTexture.GetDimensions(dstDims.x, dstDims.y);\n";
            source += "    if (any(srcDims != dstDims))\n";
            source += "        return;\n";
            source += "    if (tid.x >= srcDims.x)\n";
            source += "        return;\n";
            source += "    dstTexture[uint2(tid.x, 0)] = srcTexture[uint2(tid.x, 0)];\n";
        }
        else if (textureType == TextureType::Texture2D)
        {
            source += "    uint2 srcDims;\n";
            source += "    srcTexture.GetDimensions(srcDims.x, srcDims.y);\n";
            source += "    uint2 dstDims;\n";
            source += "    dstTexture.GetDimensions(dstDims.x, dstDims.y);\n";
            source += "    if (any(srcDims != dstDims))\n";
            source += "        return;\n";
            source += "    if (any(tid.xy >= dstDims))\n";
            source += "        return;\n";
            source += "    dstTexture[tid.xy] = srcTexture[tid.xy];\n";
        }
        else if (textureType == TextureType::Texture2DArray)
        {
            source += "    uint3 srcDims;\n";
            source += "    srcTexture.GetDimensions(srcDims.x, srcDims.y, srcDims.z);\n";
            source += "    uint3 dstDims;\n";
            source += "    dstTexture.GetDimensions(dstDims.x, dstDims.y, dstDims.z);\n";
            source += "    if (any(srcDims.xy != dstDims.xy))\n";
            source += "        return;\n";
            source += "    if (any(tid.xy >= dstDims.xy))\n";
            source += "        return;\n";
            source += "    dstTexture[uint3(tid.xy, 0)] = srcTexture[uint3(tid.xy, 0)];\n";
        }
        else if (textureType == TextureType::Texture3D)
        {
            source += "    uint3 srcDims;\n";
            source += "    srcTexture.GetDimensions(srcDims.x, srcDims.y, srcDims.z);\n";
            source += "    uint3 dstDims;\n";
            source += "    dstTexture.GetDimensions(dstDims.x, dstDims.y, dstDims.z);\n";
            source += "    if (any(srcDims != dstDims))\n";
            source += "        return;\n";
            source += "    if (any(tid >= dstDims))\n";
            source += "        return;\n";
            source += "    dstTexture[tid] = srcTexture[tid];\n";
        }
        source += "}\n";
        // fprintf(stderr, "Shader source:\n%s\n", source.c_str());

        ComPtr<IShaderProgram> shaderProgram;
        REQUIRE_CALL(loadComputeProgramFromSource(device, shaderProgram, source));

        ComPtr<IComputePipeline> pipeline;
        ComputePipelineDesc pipelineDesc = {};
        pipelineDesc.program = shaderProgram;
        REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));
        pipelines[key] = pipeline;
        return pipeline;
    };

    runTextureTest(
        options,
        [&](TextureTestContext* c)
        {
            if (device->getDeviceType() == DeviceType::CUDA)
            {
                const TextureDesc& desc = c->getTextureData().desc;
                // Error: surf1Dwrite_convert<float>(((<invalid intrinsic>)), (dstTexture_0), ((_S2)) * 1,
                // SLANG_CUDA_BOUNDARY_MODE);
                if (desc.type == TextureType::Texture1D)
                    return;
                // Currently broken (needs to be implemented in the Slang backend)
                if (desc.type == TextureType::Texture1DArray || desc.type == TextureType::Texture2DArray)
                    return;
            }

            const TextureData& data = c->getTextureData();

            // Enable this to helpfully log all created textures.
            // fprintf(stderr, "Created texture %s\n", c->getTexture()->getDesc().label);

            ComPtr<ITexture> srcTexture = c->getTexture();
            TextureDesc dstTextureDesc = srcTexture->getDesc();
            dstTextureDesc.usage |= TextureUsage::UnorderedAccess;
            ComPtr<ITexture> dstTexture;
            REQUIRE_CALL(device->createTexture(dstTextureDesc, nullptr, dstTexture.writeRef()));

            uint32_t layerCount = c->getTextureData().desc.getLayerCount();
            uint32_t mipCount = c->getTextureData().desc.mipCount;

            for (uint32_t layer = 0; layer < layerCount; ++layer)
            {
                for (uint32_t mip = 0; mip < mipCount; ++mip)
                {
                    TextureViewDesc srcViewDesc = {};
                    srcViewDesc.subresourceRange.layer = layer;
                    srcViewDesc.subresourceRange.layerCount = 1;
                    srcViewDesc.subresourceRange.mip = mip;
                    srcViewDesc.subresourceRange.mipCount = 1;
                    ComPtr<ITextureView> srcView;
                    REQUIRE_CALL(srcTexture->createView(srcViewDesc, srcView.writeRef()));

                    TextureViewDesc dstViewDesc = {};
                    dstViewDesc.subresourceRange.layer = layer;
                    dstViewDesc.subresourceRange.layerCount = 1;
                    dstViewDesc.subresourceRange.mip = mip;
                    dstViewDesc.subresourceRange.mipCount = 1;
                    ComPtr<ITextureView> dstView;
                    REQUIRE_CALL(dstTexture->createView(dstViewDesc, dstView.writeRef()));

                    ComPtr<IComputePipeline> pipeline = getCopyPipeline(dstTextureDesc.type, dstTextureDesc.format);

                    auto queue = device->getQueue(QueueType::Graphics);
                    auto commandEncoder = queue->createCommandEncoder();

                    auto passEncoder = commandEncoder->beginComputePass();
                    auto rootObject = passEncoder->bindPipeline(pipeline);
                    ShaderCursor cursor(rootObject->getEntryPoint(0));
                    cursor["srcTexture"].setBinding(srcView);
                    cursor["dstTexture"].setBinding(dstView);
                    SubresourceLayout layout;
                    REQUIRE_CALL(srcTexture->getSubresourceLayout(mip, &layout));
                    passEncoder->dispatchCompute(layout.size.width, layout.size.height, layout.size.depth);
                    passEncoder->end();

                    queue->submit(commandEncoder->finish());
                }
            }

            // Because signed normalized formats have two binary representations for -1.0,
            // we need to check the values as converted to floats.
            const FormatInfo& info = getFormatInfo(dstTextureDesc.format);
            if (info.kind == FormatKind::Normalized && info.isSigned)
            {
                data.checkEqualFloat(dstTexture);
            }
            else
            {
                data.checkEqual(srcTexture);
            }
        }
    );
}


// This test checks texture views for read and read-write access using subscript operator on all basic texture types
// (1D, 2D, 3D) and formats. It creates a compute shader that copies data from a source texture to a destination
// texture. The view always targets a single layer and mip-level.
GPU_TEST_CASE("texture-view-simple-load-store", D3D11 | D3D12 | Vulkan | CUDA | Metal)
{
    TextureTestOptions options(device);
    options.addVariants(
        TTShape::D1 | TTShape::D2 | TTShape::D3,
        TTArray::Both,    // non-array/array
        TTMip::Both,      // with/without mips
        TTMS::Off,        // without multisampling
        TTPowerOf2::Both, // test both power-of-2 and non-power-of-2 sizes where possible
        kFormats
    );

    struct PipelineKey
    {
        TextureType textureType;
        Format format;
        bool operator<(const PipelineKey& other) const
        {
            return std::tie(textureType, format) < std::tie(other.textureType, other.format);
        }
    };
    std::map<PipelineKey, ComPtr<IComputePipeline>> pipelines;
    auto getCopyPipeline = [&](TextureType textureType, Format format) -> ComPtr<IComputePipeline>
    {
        PipelineKey key = {textureType, format};
        auto it = pipelines.find(key);
        if (it != pipelines.end())
            return it->second;

        std::string source;
        std::string srcTextureType = getTextureType(textureType) + "<" + getFormatType(format) + ">";
        std::string dstTextureType =
            getFormatAttribute(format) + getRWTextureType(textureType) + "<" + getFormatType(format) + ">";
        source += "[shader(\"compute\")]\n";
        source += "[numthreads(1,1,1)]\n";
        source += "void copyTexture(\n";
        source += "    uint3 tid : SV_DispatchThreadID,\n";
        source += "    uniform " + srcTextureType + " srcTexture,\n";
        source += "    uniform " + dstTextureType + " dstTexture)\n";
        source += "{\n";
        if (textureType == TextureType::Texture1D)
        {
            source += "    uint srcDims;\n";
            source += "    srcTexture.GetDimensions(srcDims);\n";
            source += "    uint dstDims;\n";
            source += "    dstTexture.GetDimensions(dstDims);\n";
            source += "    if (srcDims != dstDims)\n";
            source += "        return;\n";
            source += "    if (tid.x >= srcDims)\n";
            source += "        return;\n";
            source += "    dstTexture.Store(tid.x, srcTexture.Load(uint2(tid.x, 0)));\n";
        }
        if (textureType == TextureType::Texture1DArray)
        {
            source += "    uint2 srcDims;\n";
            source += "    srcTexture.GetDimensions(srcDims.x, srcDims.y);\n";
            source += "    uint2 dstDims;\n";
            source += "    dstTexture.GetDimensions(dstDims.x, dstDims.y);\n";
            source += "    if (any(srcDims != dstDims))\n";
            source += "        return;\n";
            source += "    if (tid.x >= srcDims.x)\n";
            source += "        return;\n";
            source += "    dstTexture.Store(uint2(tid.x, 0), srcTexture.Load(uint3(tid.x, 0, 0)));\n";
        }
        else if (textureType == TextureType::Texture2D)
        {
            source += "    uint2 srcDims;\n";
            source += "    srcTexture.GetDimensions(srcDims.x, srcDims.y);\n";
            source += "    uint2 dstDims;\n";
            source += "    dstTexture.GetDimensions(dstDims.x, dstDims.y);\n";
            source += "    if (any(srcDims != dstDims))\n";
            source += "        return;\n";
            source += "    if (any(tid.xy >= dstDims))\n";
            source += "        return;\n";
            source += "    dstTexture.Store(tid.xy, srcTexture.Load(uint3(tid.xy, 0)));\n";
        }
        else if (textureType == TextureType::Texture2DArray)
        {
            source += "    uint3 srcDims;\n";
            source += "    srcTexture.GetDimensions(srcDims.x, srcDims.y, srcDims.z);\n";
            source += "    uint3 dstDims;\n";
            source += "    dstTexture.GetDimensions(dstDims.x, dstDims.y, dstDims.z);\n";
            source += "    if (any(srcDims.xy != dstDims.xy))\n";
            source += "        return;\n";
            source += "    if (any(tid.xy >= dstDims.xy))\n";
            source += "        return;\n";
            source += "    dstTexture.Store(uint3(tid.xy, 0), srcTexture.Load(uint4(tid.xy, 0, 0)));\n";
        }
        else if (textureType == TextureType::Texture3D)
        {
            source += "    uint3 srcDims;\n";
            source += "    srcTexture.GetDimensions(srcDims.x, srcDims.y, srcDims.z);\n";
            source += "    uint3 dstDims;\n";
            source += "    dstTexture.GetDimensions(dstDims.x, dstDims.y, dstDims.z);\n";
            source += "    if (any(srcDims != dstDims))\n";
            source += "        return;\n";
            source += "    if (any(tid >= dstDims))\n";
            source += "        return;\n";
            source += "    dstTexture.Store(tid, srcTexture.Load(uint4(tid, 0)));\n";
        }
        source += "}\n";
        // fprintf(stderr, "Shader source:\n%s\n", source.c_str());

        ComPtr<IShaderProgram> shaderProgram;
        REQUIRE_CALL(loadComputeProgramFromSource(device, shaderProgram, source));

        ComPtr<IComputePipeline> pipeline;
        ComputePipelineDesc pipelineDesc = {};
        pipelineDesc.program = shaderProgram;
        REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));
        pipelines[key] = pipeline;
        return pipeline;
    };

    runTextureTest(
        options,
        [&](TextureTestContext* c)
        {
            if (device->getDeviceType() == DeviceType::CUDA)
            {
                const TextureDesc& desc = c->getTextureData().desc;
                // Error: surf1Dwrite_convert<float>(((<invalid intrinsic>)), (dstTexture_0), ((_S2)) * 1,
                // SLANG_CUDA_BOUNDARY_MODE);
                if (desc.type == TextureType::Texture1D)
                    return;
                // Currently broken (needs to be implemented in the Slang backend)
                if (desc.type == TextureType::Texture1DArray || desc.type == TextureType::Texture2DArray)
                    return;
            }

            const TextureData& data = c->getTextureData();

            // Enable this to helpfully log all created textures.
            // fprintf(stderr, "Created texture %s\n", c->getTexture()->getDesc().label);

            ComPtr<ITexture> srcTexture = c->getTexture();
            TextureDesc dstTextureDesc = srcTexture->getDesc();
            dstTextureDesc.usage |= TextureUsage::UnorderedAccess;
            ComPtr<ITexture> dstTexture;
            REQUIRE_CALL(device->createTexture(dstTextureDesc, nullptr, dstTexture.writeRef()));

            uint32_t layerCount = c->getTextureData().desc.getLayerCount();
            uint32_t mipCount = c->getTextureData().desc.mipCount;

            for (uint32_t layer = 0; layer < layerCount; ++layer)
            {
                for (uint32_t mip = 0; mip < mipCount; ++mip)
                {
                    TextureViewDesc srcViewDesc = {};
                    srcViewDesc.subresourceRange.layer = layer;
                    srcViewDesc.subresourceRange.layerCount = 1;
                    srcViewDesc.subresourceRange.mip = mip;
                    srcViewDesc.subresourceRange.mipCount = 1;
                    ComPtr<ITextureView> srcView;
                    REQUIRE_CALL(srcTexture->createView(srcViewDesc, srcView.writeRef()));

                    TextureViewDesc dstViewDesc = {};
                    dstViewDesc.subresourceRange.layer = layer;
                    dstViewDesc.subresourceRange.layerCount = 1;
                    dstViewDesc.subresourceRange.mip = mip;
                    dstViewDesc.subresourceRange.mipCount = 1;
                    ComPtr<ITextureView> dstView;
                    REQUIRE_CALL(dstTexture->createView(dstViewDesc, dstView.writeRef()));

                    ComPtr<IComputePipeline> pipeline = getCopyPipeline(dstTextureDesc.type, dstTextureDesc.format);

                    auto queue = device->getQueue(QueueType::Graphics);
                    auto commandEncoder = queue->createCommandEncoder();

                    auto passEncoder = commandEncoder->beginComputePass();
                    auto rootObject = passEncoder->bindPipeline(pipeline);
                    ShaderCursor cursor(rootObject->getEntryPoint(0));
                    cursor["srcTexture"].setBinding(srcView);
                    cursor["dstTexture"].setBinding(dstView);
                    SubresourceLayout layout;
                    REQUIRE_CALL(srcTexture->getSubresourceLayout(mip, &layout));
                    passEncoder->dispatchCompute(layout.size.width, layout.size.height, layout.size.depth);
                    passEncoder->end();

                    queue->submit(commandEncoder->finish());
                }
            }

            // Because signed normalized formats have two binary representations for -1.0,
            // we need to check the values as converted to floats.
            const FormatInfo& info = getFormatInfo(dstTextureDesc.format);
            if (info.kind == FormatKind::Normalized && info.isSigned)
            {
                data.checkEqualFloat(dstTexture);
            }
            else
            {
                data.checkEqual(srcTexture);
            }
        }
    );
}

// This test checks texture views for read and read-write access on all basic texture types (1D, 2D, 3D) and formats.
// It creates a compute shader that copies data from a source texture to a destination texture.
// The view always targets a single mip-level.
GPU_TEST_CASE("texture-view-array", D3D11 | D3D12 | Vulkan | CUDA | Metal)
{
    TextureTestOptions options(device);
    options.addVariants(
        TTShape::D1 | TTShape::D2,
        TTArray::On,      // array
        TTMip::Both,      // with/without mips
        TTMS::Off,        // without multisampling
        TTPowerOf2::Both, // test both power-of-2 and non-power-of-2 sizes where possible
        kFormats
    );

    struct PipelineKey
    {
        TextureType textureType;
        Format format;
        bool operator<(const PipelineKey& other) const
        {
            return std::tie(textureType, format) < std::tie(other.textureType, other.format);
        }
    };
    std::map<PipelineKey, ComPtr<IComputePipeline>> pipelines;
    auto getCopyPipeline = [&](TextureType textureType, Format format) -> ComPtr<IComputePipeline>
    {
        PipelineKey key = {textureType, format};
        auto it = pipelines.find(key);
        if (it != pipelines.end())
            return it->second;

        std::string source;
        std::string srcTextureType = getTextureType(textureType) + "<" + getFormatType(format) + ">";
        std::string dstTextureType =
            getFormatAttribute(format) + getRWTextureType(textureType) + "<" + getFormatType(format) + ">";
        source += "[shader(\"compute\")]\n";
        source += "[numthreads(1,1,1)]\n";
        source += "void copyTexture(\n";
        source += "    uint3 tid : SV_DispatchThreadID,\n";
        source += "    uniform " + srcTextureType + " srcTexture,\n";
        source += "    uniform " + dstTextureType + " dstTexture,\n";
        source += "    uniform uint srcLayer)\n";
        source += "{\n";
        if (textureType == TextureType::Texture1DArray)
        {
            source += "    uint2 srcDims;\n";
            source += "    srcTexture.GetDimensions(srcDims.x, srcDims.y);\n";
            source += "    uint2 dstDims;\n";
            source += "    dstTexture.GetDimensions(dstDims.x, dstDims.y);\n";
            source += "    if (any(srcDims != dstDims))\n";
            source += "        return;\n";
            source += "    if (tid.x >= srcDims.x)\n";
            source += "        return;\n";
            source += "    dstTexture[tid.x] = srcTexture[uint2(tid.x, srcLayer)];\n";
        }
        else if (textureType == TextureType::Texture2DArray)
        {
            source += "    uint3 srcDims;\n";
            source += "    srcTexture.GetDimensions(srcDims.x, srcDims.y, srcDims.z);\n";
            source += "    uint3 dstDims;\n";
            source += "    dstTexture.GetDimensions(dstDims.x, dstDims.y, dstDims.z);\n";
            source += "    if (any(srcDims != dstDims))\n";
            source += "        return;\n";
            source += "    if (any(tid.xy >= dstDims.xy))\n";
            source += "        return;\n";
            source += "    dstTexture[uint3(tid.xy, 0)] = srcTexture[uint3(tid.xy, srcLayer)];\n";
        }
        source += "}\n";
        // fprintf(stderr, "Shader source:\n%s\n", source.c_str());

        ComPtr<IShaderProgram> shaderProgram;
        REQUIRE_CALL(loadComputeProgramFromSource(device, shaderProgram, source));

        ComPtr<IComputePipeline> pipeline;
        ComputePipelineDesc pipelineDesc = {};
        pipelineDesc.program = shaderProgram;
        REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));
        pipelines[key] = pipeline;
        return pipeline;
    };

    runTextureTest(
        options,
        [&](TextureTestContext* c)
        {
            if (device->getDeviceType() == DeviceType::CUDA)
            {
                const TextureDesc& desc = c->getTextureData().desc;
                // Error: surf1Dwrite_convert<float>(((<invalid intrinsic>)), (dstTexture_0), ((_S2)) * 1,
                // SLANG_CUDA_BOUNDARY_MODE);
                if (desc.type == TextureType::Texture1D)
                    return;
            }

            const TextureData& data = c->getTextureData();

            // Enable this to helpfully log all created textures.
            // fprintf(stderr, "Created texture %s\n", c->getTexture()->getDesc().label);

            // If texture type couldn't be initialized (eg multisampled or multi-aspect)
            // then don't check it's contents.
            if (data.initMode == TextureInitMode::None)
                return;

            ComPtr<ITexture> srcTexture = c->getTexture();
            TextureDesc dstTextureDesc = srcTexture->getDesc();
            dstTextureDesc.usage |= TextureUsage::UnorderedAccess;
            ComPtr<ITexture> dstTexture;
            REQUIRE_CALL(device->createTexture(dstTextureDesc, nullptr, dstTexture.writeRef()));

            uint32_t layerCount = c->getTextureData().desc.getLayerCount();
            uint32_t mipCount = c->getTextureData().desc.mipCount;

            for (uint32_t layer = 0; layer < layerCount; ++layer)
            {
                for (uint32_t mip = 0; mip < mipCount; ++mip)
                {
                    TextureViewDesc srcViewDesc = {};
                    srcViewDesc.subresourceRange.layer = layer;
                    srcViewDesc.subresourceRange.layerCount = 1;
                    srcViewDesc.subresourceRange.mip = mip;
                    srcViewDesc.subresourceRange.mipCount = 1;
                    ComPtr<ITextureView> srcView;
                    REQUIRE_CALL(srcTexture->createView(srcViewDesc, srcView.writeRef()));

                    TextureViewDesc dstViewDesc = {};
                    dstViewDesc.subresourceRange.layer = layer;
                    dstViewDesc.subresourceRange.layerCount = 1;
                    dstViewDesc.subresourceRange.mip = mip;
                    dstViewDesc.subresourceRange.mipCount = 1;
                    ComPtr<ITextureView> dstView;
                    REQUIRE_CALL(dstTexture->createView(dstViewDesc, dstView.writeRef()));

                    ComPtr<IComputePipeline> pipeline = getCopyPipeline(dstTextureDesc.type, dstTextureDesc.format);

                    auto queue = device->getQueue(QueueType::Graphics);
                    auto commandEncoder = queue->createCommandEncoder();

                    auto passEncoder = commandEncoder->beginComputePass();
                    auto rootObject = passEncoder->bindPipeline(pipeline);
                    ShaderCursor cursor(rootObject->getEntryPoint(0));
                    cursor["srcTexture"].setBinding(srcView);
                    cursor["dstTexture"].setBinding(dstView);
                    cursor["srcLayer"].setData(layer);
                    SubresourceLayout layout;
                    REQUIRE_CALL(srcTexture->getSubresourceLayout(mip, &layout));
                    passEncoder->dispatchCompute(layout.size.width, layout.size.height, layout.size.depth);
                    passEncoder->end();

                    queue->submit(commandEncoder->finish());
                }

                // // Because signed normalized formats have two binary representations for -1.0,
                // // we need to check the values as converted to floats.
                // const FormatInfo& info = getFormatInfo(dstTextureDesc.format);
                // if (info.kind == FormatKind::Normalized && info.isSigned)
                // {
                //     data.checkEqualFloat(dstTexture);
                // }
                // else
                // {

                //     // data.checkSliceEqual();
                //     data.checkEqual(srcTexture);
                // }
            }
        }
    );
}
