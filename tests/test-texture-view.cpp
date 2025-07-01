#include "testing.h"
#include "texture-test.h"
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

// This test checks texture views for read and read-write access on all basic texture types (1D, 2D, 3D) and formats.
// It creates a compute shader that copies data from a source texture to a destination texture.
// The view always targets a single mip-level.
GPU_TEST_CASE("texture-view-simple", D3D11 | D3D12 | Vulkan | CUDA | Metal)
{
    TextureTestOptions options(device);
    options.addVariants(
        TTShape::D1 | TTShape::D2 | TTShape::D3,
        TTArray::Off,     // non-array
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
        if (textureType == TextureType::Texture1D || textureType == TextureType::Texture1DArray)
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
        else if (textureType == TextureType::Texture2D || textureType == TextureType::Texture2DArray)
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
        else if (textureType == TextureType::Texture3D)
        {
            source += "    uint3 srcDims;\n";
            source += "    srcTexture.GetDimensions(srcDims.x, srcDims.y, srcDims.z);\n";
            source += "    uint3 dstDims;\n";
            source += "    srcTexture.GetDimensions(dstDims.x, dstDims.y, dstDims.z);\n";
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
            // TODO: There are many issues in the CUDA backend that prevent this test from passing.
            // For now, we skip the configs that produce invalid code.
            // https://github.com/shader-slang/slang/issues/7557
            if (device->getDeviceType() == DeviceType::CUDA)
            {
                const TextureDesc& desc = c->getTextureData().desc;
                // Error: surf1Dwrite_convert<float>(((<invalid intrinsic>)), (dstTexture_0), ((_S2)) * 1,
                // SLANG_CUDA_BOUNDARY_MODE);
                if (desc.type == TextureType::Texture1D)
                    return;
                // Error: cuModuleLoadData(&pipeline->m_module, module.code->getBufferPointer()) failed: a PTX JIT
                // compilation failed (CUDA_ERROR_INVALID_PTX)
                if (desc.type == TextureType::Texture3D)
                    return;
                // Error: extern inline function "surf2Dwrite_convert(T, cudaSurfaceObject_t, int, int,
                // cudaSurfaceBoundaryMode) [with T=uint]" was referenced but not defined
                if (desc.format == Format::R8Uint || desc.format == Format::R16Uint)
                    return;
                // Error: extern inline function "surf2Dwrite_convert(T, cudaSurfaceObject_t, int, int,
                // cudaSurfaceBoundaryMode) [with T=uint2]" was referenced but not defined
                if (desc.format == Format::RG8Uint || desc.format == Format::RG16Uint)
                    return;
                if (desc.format == Format::RGBA8Uint || desc.format == Format::RGBA16Uint)
                    return;
                // Error: extern inline function "surf2Dwrite_convert(T, cudaSurfaceObject_t, int, int,
                // cudaSurfaceBoundaryMode) [with T=int]" was referenced but not defined
                if (desc.format == Format::R8Sint || desc.format == Format::R16Sint)
                    return;
                if (desc.format == Format::RG8Sint || desc.format == Format::RG16Sint)
                    return;
                if (desc.format == Format::RGBA8Sint || desc.format == Format::RGBA16Sint)
                    return;
            }
            // TODO: There are many issues in the Metal backend that prevent this test from passing.
            // For now, we skip the configs that crash slang.
            // https://github.com/shader-slang/slang/issues/7558
            if (device->getDeviceType() == DeviceType::Metal)
            {
                const TextureDesc& desc = c->getTextureData().desc;
                // Error: libslang.dylib!Slang::legalizeIRForMetal(Slang::IRModule*, Slang::DiagnosticSink*)
                if (desc.format == Format::RG8Uint || desc.format == Format::RG8Sint ||
                    desc.format == Format::RG8Unorm || desc.format == Format::RG8Snorm)
                    return;
                if (desc.format == Format::RG16Uint || desc.format == Format::RG16Sint ||
                    desc.format == Format::RG16Unorm || desc.format == Format::RG16Snorm)
                    return;
                if (desc.format == Format::RG16Float)
                    return;
                if (desc.format == Format::RG32Uint || desc.format == Format::RG32Sint ||
                    desc.format == Format::RG32Float)
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
