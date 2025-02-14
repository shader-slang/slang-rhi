#include "testing.h"

#include "../src/core/string.h"

#include <map>

using namespace rhi;
using namespace rhi::testing;

Format convertTypelessFormat(Format format)
{
    switch (format)
    {
    case Format::R32G32B32A32_TYPELESS:
        return Format::R32G32B32A32_FLOAT;
    case Format::R32G32B32_TYPELESS:
        return Format::R32G32B32_FLOAT;
    case Format::R32G32_TYPELESS:
        return Format::R32G32_FLOAT;
    case Format::R32_TYPELESS:
        return Format::R32_FLOAT;
    case Format::R16G16B16A16_TYPELESS:
        return Format::R16G16B16A16_FLOAT;
    case Format::R16G16_TYPELESS:
        return Format::R16G16_FLOAT;
    case Format::R16_TYPELESS:
        return Format::R16_FLOAT;
    case Format::R8G8B8A8_TYPELESS:
        return Format::R8G8B8A8_UNORM;
    case Format::R8G8_TYPELESS:
        return Format::R8G8_UNORM;
    case Format::R8_TYPELESS:
        return Format::R8_UNORM;
    case Format::B8G8R8A8_TYPELESS:
        return Format::B8G8R8A8_UNORM;
    case Format::R10G10B10A2_TYPELESS:
        return Format::R10G10B10A2_UINT;
    default:
        return Format::Unknown;
    }
}

struct TestFormats
{
    ComPtr<IDevice> device;
    ComPtr<ISampler> sampler;
    ComPtr<IBuffer> resultBuffer;
    std::map<std::string, ComPtr<IComputePipeline>> cachedPipelines;

    void init(IDevice* device)
    {
        this->device = device;

        SamplerDesc samplerDesc;
        sampler = device->createSampler(samplerDesc);

        BufferDesc bufferDesc = {};
        bufferDesc.size = 64;
        bufferDesc.elementSize = 4; // for D3D11
        bufferDesc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess | BufferUsage::CopySource;
        bufferDesc.defaultState = ResourceState::UnorderedAccess;
        bufferDesc.memoryType = MemoryType::DeviceLocal;
        resultBuffer = device->createBuffer(bufferDesc);
    }

    bool isFormatSupported(Format format)
    {
        const FormatInfo& info = getFormatInfo(format);

        if (format == Format::R32G32B32_FLOAT || format == Format::R32G32B32_UINT || format == Format::R32G32B32_SINT ||
            format == Format::R32G32B32_TYPELESS)
            return false;

        // for WebGPU
        if (info.isTypeless)
            return false;
        if (format == Format::B4G4R4A4_UNORM || format == Format::B5G6R5_UNORM || format == Format::B5G5R5A1_UNORM)
            return false;

        return true;
    }

    ComPtr<ITextureView> createTextureView(Format format, Extents size, SubresourceData* data, int mips = 1)
    {
        TextureDesc texDesc = {};
        texDesc.type = TextureType::Texture2D;
        texDesc.mipLevelCount = mips;
        texDesc.size = size;
        texDesc.usage = TextureUsage::ShaderResource;
        texDesc.defaultState = ResourceState::ShaderResource;
        texDesc.format = format;

        ComPtr<ITexture> texture;
        REQUIRE_CALL(device->createTexture(texDesc, data, texture.writeRef()));

        ComPtr<ITextureView> view;
        TextureViewDesc viewDesc = {};
        viewDesc.format = getFormatInfo(format).isTypeless ? convertTypelessFormat(format) : format;
        REQUIRE_CALL(device->createTextureView(texture, viewDesc, view.writeRef()));
        return view;
    }

    void testFormat(
        ComPtr<ITextureView> textureView,
        ComPtr<IBuffer> buffer,
        const char* entryPoint,
        ComPtr<ISampler> sampler = nullptr
    )
    {
        ComPtr<IComputePipeline>& pipeline = cachedPipelines[entryPoint];
        if (!pipeline)
        {
            ComPtr<IShaderProgram> shaderProgram;
            slang::ProgramLayout* slangReflection;
            REQUIRE_CALL(loadComputeProgram(device, shaderProgram, "test-formats", entryPoint, slangReflection));

            ComputePipelineDesc pipelineDesc = {};
            pipelineDesc.program = shaderProgram.get();
            REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));
        }

        // We have done all the set up work, now it is time to start recording a command buffer for
        // GPU execution.
        {
            auto queue = device->getQueue(QueueType::Graphics);
            auto commandEncoder = queue->createCommandEncoder();

            auto passEncoder = commandEncoder->beginComputePass();
            auto rootObject = passEncoder->bindPipeline(pipeline);
            ShaderCursor cursor(rootObject->getEntryPoint(0)); // get a cursor the the first entry-point.
            // Bind texture view to the entry point
            cursor["tex"].setBinding(textureView);
            if (sampler)
                cursor["sampler"].setBinding(sampler);
            // Bind buffer view to the entry point.
            cursor["buffer"].setBinding(buffer);
            passEncoder->dispatchCompute(1, 1, 1);
            passEncoder->end();

            queue->submit(commandEncoder->finish());
            queue->waitOnHost();
        }
    }

    template<typename T, size_t Count>
    void testFormat(
        Format format,
        Extents textureSize,
        SubresourceData* textureData,
        const std::array<T, Count>& expected
    )
    {
        if (!isFormatSupported(format))
        {
            return;
        }

        const FormatInfo& info = getFormatInfo(format);

        // MESSAGE("Checking format: ", doctest::String(info.name));

        ComPtr<ITextureView> textureView = createTextureView(format, textureSize, textureData);

        if constexpr (std::is_same_v<T, float>)
        {
            std::string entryPointName = "copyTexFloat" + std::to_string(info.channelCount);
            testFormat(textureView, resultBuffer, entryPointName.c_str());
            compareComputeResult(device, resultBuffer, expected);
        }
        else if constexpr (std::is_same_v<T, uint32_t>)
        {
            std::string entryPointName = "copyTexUint" + std::to_string(info.channelCount);
            testFormat(textureView, resultBuffer, entryPointName.c_str());
            compareComputeResult(device, resultBuffer, expected);
        }
        else if constexpr (std::is_same_v<T, int32_t>)
        {
            std::string entryPointName = "copyTexInt" + std::to_string(info.channelCount);
            testFormat(textureView, resultBuffer, entryPointName.c_str());
            compareComputeResult(device, resultBuffer, expected);
        }
    }

    void run()
    {
        Extents size = {};
        size.width = 2;
        size.height = 2;
        size.depth = 1;

        Extents bcSize = {};
        bcSize.width = 4;
        bcSize.height = 4;
        bcSize.depth = 1;

        // Note: D32_FLOAT and D16_UNORM are not directly tested as they are only used for raster. These
        // are the same as R32_FLOAT and R16_UNORM, respectively, when passed to a shader.

        {
            // clang-format off
            float texData[] = {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.5f, 0.5f, 0.5f, 1.0f};
            SubresourceData subData = {(void*)texData, 32, 0};
            std::array expected = {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.5f, 0.5f, 0.5f, 1.0f};
            // clang-format on

            testFormat(Format::R32G32B32A32_FLOAT, size, &subData, expected);
            testFormat(Format::R32G32B32A32_TYPELESS, size, &subData, expected);
        }

        {
            float texData[] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.5f, 0.5f, 0.5f};
            SubresourceData subData = {(void*)texData, 24, 0};
            std::array expected = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.5f, 0.5f, 0.5f};

            testFormat(Format::R32G32B32_FLOAT, size, &subData, expected);
            testFormat(Format::R32G32B32_TYPELESS, size, &subData, expected);
        }

        {
            float texData[] = {1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.5f, 0.5f};
            SubresourceData subData = {(void*)texData, 16, 0};
            std::array expected = {1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.5f, 0.5f};

            testFormat(Format::R32G32_FLOAT, size, &subData, expected);
            testFormat(Format::R32G32_TYPELESS, size, &subData, expected);
        }

        {
            float texData[] = {1.0f, 0.0f, 0.5f, 0.25f};
            SubresourceData subData = {(void*)texData, 8, 0};
            std::array expected = {1.0f, 0.0f, 0.5f, 0.25f};

            testFormat(Format::R32_FLOAT, size, &subData, expected);
            testFormat(Format::R32_TYPELESS, size, &subData, expected);
        }

        {
            // clang-format off
            uint16_t texData[] = {15360u, 0u, 0u, 15360u, 0u, 15360u, 0u, 15360u, 0u, 0u, 15360u, 15360u, 14336u, 14336u, 14336u, 15360u};
            SubresourceData subData = {(void*)texData, 16, 0};
            std::array expected = {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.5f, 0.5f, 0.5f, 1.0f};
            // clang-format on

            testFormat(Format::R16G16B16A16_FLOAT, size, &subData, expected);
            testFormat(Format::R16G16B16A16_TYPELESS, size, &subData, expected);
        }

        {
            uint16_t texData[] = {15360u, 0u, 0u, 15360u, 15360u, 15360u, 14336u, 14336u};
            SubresourceData subData = {(void*)texData, 8, 0};
            std::array expected = {1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.5f, 0.5f};

            testFormat(Format::R16G16_FLOAT, size, &subData, expected);
            testFormat(Format::R16G16_TYPELESS, size, &subData, expected);
        }

        {
            uint16_t texData[] = {15360u, 0u, 14336u, 13312u};
            SubresourceData subData = {(void*)texData, 4, 0};
            std::array expected = {1.0f, 0.0f, 0.5f, 0.25f};

            testFormat(Format::R16_FLOAT, size, &subData, expected);
            testFormat(Format::R16_TYPELESS, size, &subData, expected);
        }

        {
            uint32_t texData[] = {255u, 0u, 0u, 255u, 0u, 255u, 0u, 255u, 0u, 0u, 255u, 255u, 127u, 127u, 127u, 255u};
            SubresourceData subData = {(void*)texData, 32, 0};
            std::array expected = {255u, 0u, 0u, 255u, 0u, 255u, 0u, 255u, 0u, 0u, 255u, 255u, 127u, 127u, 127u, 255u};

            testFormat(Format::R32G32B32A32_UINT, size, &subData, expected);
        }

        {
            uint32_t texData[] = {255u, 0u, 0u, 0u, 255u, 0u, 0u, 0u, 255u, 127u, 127u, 127u};
            SubresourceData subData = {(void*)texData, 24, 0};
            std::array expected = {255u, 0u, 0u, 0u, 255u, 0u, 0u, 0u, 255u, 127u, 127u, 127u};

            testFormat(Format::R32G32B32_UINT, size, &subData, expected);
        }

        {
            uint32_t texData[] = {255u, 0u, 0u, 255u, 255u, 255u, 127u, 127u};
            SubresourceData subData = {(void*)texData, 16, 0};
            std::array expected = {255u, 0u, 0u, 255u, 255u, 255u, 127u, 127u};

            testFormat(Format::R32G32_UINT, size, &subData, expected);
        }

        {
            uint32_t texData[] = {255u, 0u, 127u, 73u};
            SubresourceData subData = {(void*)texData, 8, 0};
            std::array expected = {255u, 0u, 127u, 73u};

            testFormat(Format::R32_UINT, size, &subData, expected);
        }

        {
            uint16_t texData[] = {255u, 0u, 0u, 255u, 0u, 255u, 0u, 255u, 0u, 0u, 255u, 255u, 127u, 127u, 127u, 255u};
            SubresourceData subData = {(void*)texData, 16, 0};
            std::array expected = {255u, 0u, 0u, 255u, 0u, 255u, 0u, 255u, 0u, 0u, 255u, 255u, 127u, 127u, 127u, 255u};

            testFormat(Format::R16G16B16A16_UINT, size, &subData, expected);
        }

        {
            uint16_t texData[] = {255u, 0u, 0u, 255u, 255u, 255u, 127u, 127u};
            SubresourceData subData = {(void*)texData, 8, 0};
            std::array expected = {255u, 0u, 0u, 255u, 255u, 255u, 127u, 127u};

            testFormat(Format::R16G16_UINT, size, &subData, expected);
        }

        {
            uint16_t texData[] = {255u, 0u, 127u, 73u};
            SubresourceData subData = {(void*)texData, 4, 0};
            std::array expected = {255u, 0u, 127u, 73u};

            testFormat(Format::R16_UINT, size, &subData, expected);
        }

        {
            uint8_t texData[] = {255u, 0u, 0u, 255u, 0u, 255u, 0u, 255u, 0u, 0u, 255u, 255u, 127u, 127u, 127u, 255u};
            SubresourceData subData = {(void*)texData, 8, 0};
            std::array expected = {255u, 0u, 0u, 255u, 0u, 255u, 0u, 255u, 0u, 0u, 255u, 255u, 127u, 127u, 127u, 255u};

            testFormat(Format::R8G8B8A8_UINT, size, &subData, expected);
        }

        {
            uint8_t texData[] = {255u, 0u, 0u, 255u, 255u, 255u, 127u, 127u};
            SubresourceData subData = {(void*)texData, 4, 0};
            std::array expected = {255u, 0u, 0u, 255u, 255u, 255u, 127u, 127u};

            testFormat(Format::R8G8_UINT, size, &subData, expected);
        }

        {
            uint8_t texData[] = {255u, 0u, 127u, 73u};
            SubresourceData subData = {(void*)texData, 2, 0};
            std::array expected = {255u, 0u, 127u, 73u};

            testFormat(Format::R8_UINT, size, &subData, expected);
        }

        {
            int32_t texData[] = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 127, 127, 127, 255};
            SubresourceData subData = {(void*)texData, 32, 0};
            std::array expected = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 127, 127, 127, 255};

            testFormat(Format::R32G32B32A32_SINT, size, &subData, expected);
        }

        {
            int32_t texData[] = {255, 0, 0, 0, 255, 0, 0, 0, 255, 127, 127, 127};
            SubresourceData subData = {(void*)texData, 24, 0};
            std::array expected = {255, 0, 0, 0, 255, 0, 0, 0, 255, 127, 127, 127};

            testFormat(Format::R32G32B32_SINT, size, &subData, expected);
        }

        {
            int32_t texData[] = {255, 0, 0, 255, 255, 255, 127, 127};
            SubresourceData subData = {(void*)texData, 16, 0};
            std::array expected = {255, 0, 0, 255, 255, 255, 127, 127};

            testFormat(Format::R32G32_SINT, size, &subData, expected);
        }

        {
            int32_t texData[] = {255, 0, 127, 73};
            SubresourceData subData = {(void*)texData, 8, 0};
            std::array expected = {255, 0, 127, 73};

            testFormat(Format::R32_SINT, size, &subData, expected);
        }

        {
            int16_t texData[] = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 127, 127, 127, 255};
            SubresourceData subData = {(void*)texData, 16, 0};
            std::array expected = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 127, 127, 127, 255};

            testFormat(Format::R16G16B16A16_SINT, size, &subData, expected);
        }

        {
            int16_t texData[] = {255, 0, 0, 255, 255, 255, 127, 127};
            SubresourceData subData = {(void*)texData, 8, 0};
            std::array expected = {255, 0, 0, 255, 255, 255, 127, 127};

            testFormat(Format::R16G16_SINT, size, &subData, expected);
        }

        {
            int16_t texData[] = {255, 0, 127, 73};
            SubresourceData subData = {(void*)texData, 4, 0};
            std::array expected = {255, 0, 127, 73};

            testFormat(Format::R16_SINT, size, &subData, expected);
        }

        {
            int8_t texData[] = {127, 0, 0, 127, 0, 127, 0, 127, 0, 0, 127, 127, 0, 0, 0, 127};
            SubresourceData subData = {(void*)texData, 8, 0};
            std::array expected = {127, 0, 0, 127, 0, 127, 0, 127, 0, 0, 127, 127, 0, 0, 0, 127};

            testFormat(Format::R8G8B8A8_SINT, size, &subData, expected);
        }

        {
            int8_t texData[] = {127, 0, 0, 127, 127, 127, 73, 73};
            SubresourceData subData = {(void*)texData, 4, 0};
            std::array expected = {127, 0, 0, 127, 127, 127, 73, 73};

            testFormat(Format::R8G8_SINT, size, &subData, expected);
        }

        {
            int8_t texData[] = {127, 0, 73, 25};
            SubresourceData subData = {(void*)texData, 2, 0};
            std::array expected = {127, 0, 73, 25};

            testFormat(Format::R8_SINT, size, &subData, expected);
        }

        {
            // clang-format off
            uint16_t texData[] = {65535u, 0u, 0u, 65535u, 0u, 65535u, 0u, 65535u, 0u, 0u, 65535u, 65535u, 32767u, 32767u, 32767u, 32767u};
            SubresourceData subData = {(void*)texData, 16, 0};
            std::array expected = {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.499992371f, 0.499992371f, 0.499992371f, 0.499992371f};
            // clang-format on

            testFormat(Format::R16G16B16A16_UNORM, size, &subData, expected);
        }

        {
            uint16_t texData[] = {65535u, 0u, 0u, 65535u, 65535u, 65535u, 32767u, 32767u};
            SubresourceData subData = {(void*)texData, 8, 0};
            std::array expected = {1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.499992371f, 0.499992371f};

            testFormat(Format::R16G16_UNORM, size, &subData, expected);
        }

        {
            uint16_t texData[] = {65535u, 0u, 32767u, 16383u};
            SubresourceData subData = {(void*)texData, 4, 0};
            std::array expected = {1.0f, 0.0f, 0.499992371f, 0.249988556f};

            testFormat(Format::R16_UNORM, size, &subData, expected);
        }

        {
            // clang-format off
            uint8_t texData[] = {0u, 0u, 0u, 255u, 127u, 127u, 127u, 255u, 255u, 255u, 255u, 255u, 0u, 0u, 0u, 0u};
            SubresourceData subData = {(void*)texData, 8, 0};
            std::array expected = {0.0f, 0.0f, 0.0f, 1.0f, 0.498039216f, 0.498039216f, 0.498039216f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f};
            std::array expectedSRGB = {0.0f, 0.0f, 0.0f, 1.0f, 0.211914062f, 0.211914062f, 0.211914062f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f};
            // clang-format on

            testFormat(Format::R8G8B8A8_UNORM, size, &subData, expected);
            testFormat(Format::R8G8B8A8_TYPELESS, size, &subData, expected);
            testFormat(Format::R8G8B8A8_UNORM_SRGB, size, &subData, expectedSRGB);
        }

        {
            uint8_t texData[] = {255u, 0u, 0u, 255u, 255u, 255u, 127u, 127u};
            SubresourceData subData = {(void*)texData, 4, 0};
            std::array expected = {1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.498039216f, 0.498039216f};

            testFormat(Format::R8G8_UNORM, size, &subData, expected);
            testFormat(Format::R8G8_TYPELESS, size, &subData, expected);
        }

        {
            uint8_t texData[] = {255u, 0u, 127u, 63u};
            SubresourceData subData = {(void*)texData, 2, 0};
            std::array expected = {1.0f, 0.0f, 0.498039216f, 0.247058824f};

            testFormat(Format::R8_UNORM, size, &subData, expected);
            testFormat(Format::R8_TYPELESS, size, &subData, expected);
        }

        {
            // clang-format off
            uint8_t texData[] = {0u, 0u, 0u, 255u, 127u, 127u, 127u, 255u, 255u, 255u, 255u, 255u, 0u, 0u, 0u, 0u};
            SubresourceData subData = {(void*)texData, 8, 0};
            std::array expected = {0.0f, 0.0f, 0.0f, 1.0f, 0.498039216f, 0.498039216f, 0.498039216f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f};
            std::array expectedSRGB = {0.0f, 0.0f, 0.0f, 1.0f, 0.211914062f, 0.211914062f, 0.211914062f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f};
            // clang-format on

            testFormat(Format::B8G8R8A8_UNORM, size, &subData, expected);
            testFormat(Format::B8G8R8A8_TYPELESS, size, &subData, expected);
            testFormat(Format::B8G8R8A8_UNORM_SRGB, size, &subData, expectedSRGB);
        }

        {
            // clang-format off
            int16_t texData[] = {32767, 0, 0, 32767, 0, 32767, 0, 32767, 0, 0, 32767, 32767, -32768, -32768, 0, 32767};
            SubresourceData subData = {(void*)texData, 16, 0};
            std::array expected = {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, -1.0f, -1.0f, 0.0f, 1.0f};
            // clang-format on

            testFormat(Format::R16G16B16A16_SNORM, size, &subData, expected);
        }

        {
            int16_t texData[] = {32767, 0, 0, 32767, 32767, 32767, -32768, -32768};
            SubresourceData subData = {(void*)texData, 8, 0};
            std::array expected = {1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, -1.0f, -1.0f};

            testFormat(Format::R16G16_SNORM, size, &subData, expected);
        }

        {
            int16_t texData[] = {32767, 0, -32768, 0};
            SubresourceData subData = {(void*)texData, 4, 0};
            std::array expected = {1.0f, 0.0f, -1.0f, 0.0f};

            testFormat(Format::R16_SNORM, size, &subData, expected);
        }

        {
            // clang-format off
            int8_t texData[] = {127, 0, 0, 127, 0, 127, 0, 127, 0, 0, 127, 127, -128, -128, 0, 127};
            SubresourceData subData = {(void*)texData, 8, 0};
            std::array expected = {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, -1.0f, -1.0f, 0.0f, 1.0f};
            // clang-format on

            testFormat(Format::R8G8B8A8_SNORM, size, &subData, expected);
        }

        {
            int8_t texData[] = {127, 0, 0, 127, 127, 127, -128, -128};
            SubresourceData subData = {(void*)texData, 4, 0};
            std::array expected = {1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, -1.0f, -1.0f};

            testFormat(Format::R8G8_SNORM, size, &subData, expected);
        }

        {
            int8_t texData[] = {127, 0, -128, 0};
            SubresourceData subData = {(void*)texData, 2, 0};
            std::array expected = {1.0f, 0.0f, -1.0f, 0.0f};

            testFormat(Format::R8_SNORM, size, &subData, expected);
        }

        {
            // clang-format off
            uint8_t texData[] = {15u, 240u, 240u, 240u, 0u, 255u, 119u, 119u};
            SubresourceData subData = {(void*)texData, 4, 0};
            std::array expected = {0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.466666669f, 0.466666669f, 0.466666669f, 0.466666669f};
            // clang-format on

            testFormat(Format::B4G4R4A4_UNORM, size, &subData, expected);
        }

        {
            // clang-format off
            uint16_t texData[] = {31u, 2016u, 63488u, 31727u};
            SubresourceData subData = {(void*)texData, 4, 0};
            std::array expected = {0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.482352942f, 0.490196079f, 0.482352942f};
            // clang-format on

            testFormat(Format::B5G6R5_UNORM, size, &subData, expected);
        }

        {
            // clang-format off
            uint16_t texData[] = {31u, 2016u, 63488u, 31727u};
            SubresourceData subData = {(void*)texData, 4, 0};
            std::array expected = {0.0f, 0.0f, 1.0f, 0.0f, 0.0313725509f, 1.0f, 0.0f, 0.0f, 0.968627453f, 0.0f, 0.0f, 1.0f, 0.968627453f, 1.0f, 0.482352942f, 0.0f};
            // clang-format on

            testFormat(Format::B5G5R5A1_UNORM, size, &subData, expected);
        }

        {
            // clang-format off
            uint32_t texData[] = {2950951416u, 2013265920u, 3086219772u, 3087007228u};
            SubresourceData subData = {(void*)texData, 8, 0};
            std::array expected = {63.0f, 63.0f, 63.0f, 0.0f, 0.0f, 0.0f, 127.0f, 127.0f, 127.0f, 127.0f, 127.5f, 127.75f};
            // clang-format on

            testFormat(Format::R9G9B9E5_SHAREDEXP, size, &subData, expected);
        }

        {
            uint32_t texData[] = {4294967295u, 0u, 2683829759u, 1193046471u};
            SubresourceData subData = {(void*)texData, 8, 0};
            std::array expected = {1023u, 1023u, 1023u, 3u, 0u, 0u, 0u, 0u, 511u, 511u, 511u, 2u, 455u, 796u, 113u, 1u};

            testFormat(Format::R10G10B10A2_TYPELESS, size, &subData, expected);
        }

        {
            // clang-format off
            uint32_t texData[] = {4294967295u, 0u, 2683829759u, 1193046471u};
            SubresourceData subData = {(void*)texData, 8, 0};
            std::array expected = {1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.499511242f, 0.499511242f, 0.499511242f, 0.66666668f, 0.444770277f, 0.778103590f, 0.110459432f, 0.333333343f};
            // clang-format on

            testFormat(Format::R10G10B10A2_UNORM, size, &subData, expected);
        }

        {
            uint32_t texData[] = {4294967295u, 0u, 2683829759u, 1193046471u};
            SubresourceData subData = {(void*)texData, 8, 0};
            std::array expected = {1023u, 1023u, 1023u, 3u, 0u, 0u, 0u, 0u, 511u, 511u, 511u, 2u, 455u, 796u, 113u, 1u};

            testFormat(Format::R10G10B10A2_UINT, size, &subData, expected);
        }

        {
            uint32_t texData[] = {3085827519u, 0u, 2951478655u, 1880884096u};
            SubresourceData subData = {(void*)texData, 8, 0};
            std::array expected = {254.0f, 254.0f, 252.0f, 0.0f, 0.0f, 0.0f, 127.0f, 127.0f, 126.0f, 0.5f, 0.5f, 0.5f};

            testFormat(Format::R11G11B10_FLOAT, size, &subData, expected);
        }

#if 0
        {
            uint8_t texData[] = {255u, 255u, 255u, 255u, 255u, 255u, 255u, 255u};
            SubresourceData subData = {(void*)texData, 8, 0};
            std::array expected = {0.0f, 0.0f, 0.517647088f, 1.0f};
            std::array expectedSRGB = {0.0f, 0.0f, 0.230468750f, 1.0f};

            testFormat(Format::BC1_UNORM, bcSize, &subData, expected);
            testFormat(Format::BC1_UNORM_SRGB, bcSize, &subData, expectedSRGB);
        }
#endif

        {
            uint8_t texData[] = {255u, 255u, 255u, 255u, 255u, 255u, 255u, 255u, 16u, 0u, 0u, 0u, 0u, 0u, 0u, 0u};
            SubresourceData subData = {(void*)texData, 16, 0};
            std::array expected = {0.0f, 0.0f, 0.517647088f, 1.0f};
            std::array expectedSRGB = {0.0f, 0.0f, 0.230468750f, 1.0f};

            testFormat(Format::BC2_UNORM, bcSize, &subData, expected);
            testFormat(Format::BC2_UNORM_SRGB, bcSize, &subData, expectedSRGB);
        }

        {
            uint8_t texData[] = {0u, 255u, 255u, 255u, 255u, 255u, 255u, 255u, 16u, 0u, 0u, 0u, 0u, 0u, 0u, 0u};
            SubresourceData subData = {(void*)texData, 16, 0};
            std::array expected = {0.0f, 0.0f, 0.517647088f, 1.0f};
            std::array expectedSRGB = {0.0f, 0.0f, 0.230468750f, 1.0f};

            testFormat(Format::BC3_UNORM, bcSize, &subData, expected);
            testFormat(Format::BC3_UNORM_SRGB, bcSize, &subData, expectedSRGB);
        }

        {
            uint8_t texData[] = {127u, 0u, 0u, 0u, 0u, 0u, 0u, 0u};
            SubresourceData subData = {(void*)texData, 8, 0};
            std::array expectedUNORM = {0.498039216f};
            std::array expectedSNORM = {1.0f};

            testFormat(Format::BC4_UNORM, bcSize, &subData, expectedUNORM);
            testFormat(Format::BC4_SNORM, bcSize, &subData, expectedSNORM);
        }

        {
            uint8_t texData[] = {127u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 127u, 0u, 0u, 0u, 0u, 0u, 0u, 0u};
            SubresourceData subData = {(void*)texData, 16, 0};
            std::array expectedUNORM = {0.498039216f, 0.498039216f, 0.498039216f, 0.498039216f};
            std::array expectedSNORM = {1.0f, 1.0f, 1.0f, 1.0f};

            testFormat(Format::BC5_UNORM, bcSize, &subData, expectedUNORM);
            testFormat(Format::BC5_SNORM, bcSize, &subData, expectedSNORM);
        }

        {
            // clang-format off
            uint8_t texData[] = {98u, 238u, 232u, 77u, 240u, 66u, 148u, 31u, 124u, 95u, 2u, 224u, 255u, 107u, 77u, 250u};
            SubresourceData subData = {(void*)texData, 16, 0};
            std::array expected = {0.343261719f, 0.897949219f, 2.16406250f};
            // clang-format on

            testFormat(Format::BC6H_UF16, bcSize, &subData, expected);
        }

        {
            // clang-format off
            uint8_t texData[] = {107u, 238u, 232u, 77u, 240u, 71u, 128u, 127u, 1u, 0u, 255u, 255u, 170u, 218u, 221u, 254u};
            SubresourceData subData = {(void*)texData, 16, 0};
            std::array expected = {0.343261719f, 0.897949219f, 2.16406250f};
            // clang-format on

            testFormat(Format::BC6H_SF16, bcSize, &subData, expected);
        }

        {
            uint8_t texData[] = {104u, 0u, 0u, 0u, 64u, 163u, 209u, 104u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u};
            SubresourceData subData = {(void*)texData, 16, 0};
            std::array expected = {0.0f, 0.101960786f, 0.0f, 1.0f};
            std::array expectedSRGB = {0.0f, 0.0103149414f, 0.0f, 1.0f};

            testFormat(Format::BC7_UNORM, bcSize, &subData, expected);
            testFormat(Format::BC7_UNORM_SRGB, bcSize, &subData, expectedSRGB);
        }
    }
};

// skip CPU: Vector types not implemented
// skip CUDA: GetDimensions not implemented
GPU_TEST_CASE("formats", D3D11 | D3D12 | Vulkan | Metal | WGPU)
{
    TestFormats test;
    test.init(device);
    test.run();
}
