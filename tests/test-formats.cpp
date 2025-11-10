#include "testing.h"

#include "../src/core/string.h"

#include <map>

using namespace rhi;
using namespace rhi::testing;

struct TestFormats
{
    ComPtr<IDevice> device;
    ComPtr<IBuffer> resultBuffer;
    std::map<std::string, ComPtr<IComputePipeline>> cachedPipelines;

    void init(IDevice* device_)
    {
        this->device = device_;

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
        FormatSupport formatSupport = FormatSupport::None;
        REQUIRE_CALL(device->getFormatSupport(format, &formatSupport));

        if (!is_set(formatSupport, FormatSupport::Texture))
        {
            return false;
        }

        return true;
    }

    ComPtr<ITextureView> createTextureView(Format format, Extent3D size, SubresourceData* data, int mips = 1)
    {
        TextureDesc texDesc = {};
        texDesc.type = TextureType::Texture2D;
        texDesc.mipCount = mips;
        texDesc.size = size;
        texDesc.usage = TextureUsage::ShaderResource;
        texDesc.defaultState = ResourceState::ShaderResource;
        texDesc.format = format;

        ComPtr<ITexture> texture;
        REQUIRE_CALL(device->createTexture(texDesc, data, texture.writeRef()));

        ComPtr<ITextureView> view;
        TextureViewDesc viewDesc = {};
        viewDesc.format = format;
        REQUIRE_CALL(device->createTextureView(texture, viewDesc, view.writeRef()));
        return view;
    }

    void testFormat(ComPtr<ITextureView> textureView, ComPtr<IBuffer> buffer, const char* entryPoint)
    {
        ComPtr<IComputePipeline>& pipeline = cachedPipelines[entryPoint];
        if (!pipeline)
        {
            ComPtr<IShaderProgram> shaderProgram;
            REQUIRE_CALL(loadProgram(device, "test-formats", entryPoint, shaderProgram.writeRef()));

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
        Extent3D textureSize,
        SubresourceData* textureData,
        const std::array<T, Count>& expected
    )
    {
        if (!isFormatSupported(format))
        {
            return;
        }

        const FormatInfo& info = getFormatInfo(format);

        CAPTURE(format);

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
        Extent3D size = {};
        size.width = 2;
        size.height = 2;
        size.depth = 1;

        Extent3D bcSize = {};
        bcSize.width = 4;
        bcSize.height = 4;
        bcSize.depth = 1;

        // Note: D32Float and D16Unorm are not directly tested as they are only used for raster. These
        // are the same as R32Float and R16Unorm, respectively, when passed to a shader.

        {
            // clang-format off
            float texData[] = {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.5f, 0.5f, 0.5f, 1.0f};
            SubresourceData subData = {(void*)texData, 32, 0};
            std::array expected = {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.5f, 0.5f, 0.5f, 1.0f};
            // clang-format on

            testFormat(Format::RGBA32Float, size, &subData, expected);
        }

        {
            float texData[] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.5f, 0.5f, 0.5f};
            SubresourceData subData = {(void*)texData, 24, 0};
            std::array expected = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.5f, 0.5f, 0.5f};

            testFormat(Format::RGB32Float, size, &subData, expected);
        }

        {
            float texData[] = {1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.5f, 0.5f};
            SubresourceData subData = {(void*)texData, 16, 0};
            std::array expected = {1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.5f, 0.5f};

            testFormat(Format::RG32Float, size, &subData, expected);
        }

        {
            float texData[] = {1.0f, 0.0f, 0.5f, 0.25f};
            SubresourceData subData = {(void*)texData, 8, 0};
            std::array expected = {1.0f, 0.0f, 0.5f, 0.25f};

            testFormat(Format::R32Float, size, &subData, expected);
        }

        {
            // clang-format off
            uint16_t texData[] = {15360u, 0u, 0u, 15360u, 0u, 15360u, 0u, 15360u, 0u, 0u, 15360u, 15360u, 14336u, 14336u, 14336u, 15360u};
            SubresourceData subData = {(void*)texData, 16, 0};
            std::array expected = {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.5f, 0.5f, 0.5f, 1.0f};
            // clang-format on

            testFormat(Format::RGBA16Float, size, &subData, expected);
        }

        {
            uint16_t texData[] = {15360u, 0u, 0u, 15360u, 15360u, 15360u, 14336u, 14336u};
            SubresourceData subData = {(void*)texData, 8, 0};
            std::array expected = {1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.5f, 0.5f};

            testFormat(Format::RG16Float, size, &subData, expected);
        }

        {
            uint16_t texData[] = {15360u, 0u, 14336u, 13312u};
            SubresourceData subData = {(void*)texData, 4, 0};
            std::array expected = {1.0f, 0.0f, 0.5f, 0.25f};

            testFormat(Format::R16Float, size, &subData, expected);
        }

        {
            uint32_t texData[] = {255u, 0u, 0u, 255u, 0u, 255u, 0u, 255u, 0u, 0u, 255u, 255u, 127u, 127u, 127u, 255u};
            SubresourceData subData = {(void*)texData, 32, 0};
            std::array expected = {255u, 0u, 0u, 255u, 0u, 255u, 0u, 255u, 0u, 0u, 255u, 255u, 127u, 127u, 127u, 255u};

            testFormat(Format::RGBA32Uint, size, &subData, expected);
        }

        {
            uint32_t texData[] = {255u, 0u, 0u, 0u, 255u, 0u, 0u, 0u, 255u, 127u, 127u, 127u};
            SubresourceData subData = {(void*)texData, 24, 0};
            std::array expected = {255u, 0u, 0u, 0u, 255u, 0u, 0u, 0u, 255u, 127u, 127u, 127u};

            testFormat(Format::RGB32Uint, size, &subData, expected);
        }

        {
            uint32_t texData[] = {255u, 0u, 0u, 255u, 255u, 255u, 127u, 127u};
            SubresourceData subData = {(void*)texData, 16, 0};
            std::array expected = {255u, 0u, 0u, 255u, 255u, 255u, 127u, 127u};

            testFormat(Format::RG32Uint, size, &subData, expected);
        }

        {
            uint32_t texData[] = {255u, 0u, 127u, 73u};
            SubresourceData subData = {(void*)texData, 8, 0};
            std::array expected = {255u, 0u, 127u, 73u};

            testFormat(Format::R32Uint, size, &subData, expected);
        }

        {
            uint16_t texData[] = {255u, 0u, 0u, 255u, 0u, 255u, 0u, 255u, 0u, 0u, 255u, 255u, 127u, 127u, 127u, 255u};
            SubresourceData subData = {(void*)texData, 16, 0};
            std::array expected = {255u, 0u, 0u, 255u, 0u, 255u, 0u, 255u, 0u, 0u, 255u, 255u, 127u, 127u, 127u, 255u};

            testFormat(Format::RGBA16Uint, size, &subData, expected);
        }

        {
            uint16_t texData[] = {255u, 0u, 0u, 255u, 255u, 255u, 127u, 127u};
            SubresourceData subData = {(void*)texData, 8, 0};
            std::array expected = {255u, 0u, 0u, 255u, 255u, 255u, 127u, 127u};

            testFormat(Format::RG16Uint, size, &subData, expected);
        }

        {
            uint16_t texData[] = {255u, 0u, 127u, 73u};
            SubresourceData subData = {(void*)texData, 4, 0};
            std::array expected = {255u, 0u, 127u, 73u};

            testFormat(Format::R16Uint, size, &subData, expected);
        }

        {
            uint8_t texData[] = {255u, 0u, 0u, 255u, 0u, 255u, 0u, 255u, 0u, 0u, 255u, 255u, 127u, 127u, 127u, 255u};
            SubresourceData subData = {(void*)texData, 8, 0};
            std::array expected = {255u, 0u, 0u, 255u, 0u, 255u, 0u, 255u, 0u, 0u, 255u, 255u, 127u, 127u, 127u, 255u};

            testFormat(Format::RGBA8Uint, size, &subData, expected);
        }

        {
            uint8_t texData[] = {255u, 0u, 0u, 255u, 255u, 255u, 127u, 127u};
            SubresourceData subData = {(void*)texData, 4, 0};
            std::array expected = {255u, 0u, 0u, 255u, 255u, 255u, 127u, 127u};

            testFormat(Format::RG8Uint, size, &subData, expected);
        }

        {
            uint8_t texData[] = {255u, 0u, 127u, 73u};
            SubresourceData subData = {(void*)texData, 2, 0};
            std::array expected = {255u, 0u, 127u, 73u};

            testFormat(Format::R8Uint, size, &subData, expected);
        }

        {
            int32_t texData[] = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 127, 127, 127, 255};
            SubresourceData subData = {(void*)texData, 32, 0};
            std::array expected = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 127, 127, 127, 255};

            testFormat(Format::RGBA32Sint, size, &subData, expected);
        }

        {
            int32_t texData[] = {255, 0, 0, 0, 255, 0, 0, 0, 255, 127, 127, 127};
            SubresourceData subData = {(void*)texData, 24, 0};
            std::array expected = {255, 0, 0, 0, 255, 0, 0, 0, 255, 127, 127, 127};

            testFormat(Format::RGB32Sint, size, &subData, expected);
        }

        {
            int32_t texData[] = {255, 0, 0, 255, 255, 255, 127, 127};
            SubresourceData subData = {(void*)texData, 16, 0};
            std::array expected = {255, 0, 0, 255, 255, 255, 127, 127};

            testFormat(Format::RG32Sint, size, &subData, expected);
        }

        {
            int32_t texData[] = {255, 0, 127, 73};
            SubresourceData subData = {(void*)texData, 8, 0};
            std::array expected = {255, 0, 127, 73};

            testFormat(Format::R32Sint, size, &subData, expected);
        }

        {
            int16_t texData[] = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 127, 127, 127, 255};
            SubresourceData subData = {(void*)texData, 16, 0};
            std::array expected = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 127, 127, 127, 255};

            testFormat(Format::RGBA16Sint, size, &subData, expected);
        }

        {
            int16_t texData[] = {255, 0, 0, 255, 255, 255, 127, 127};
            SubresourceData subData = {(void*)texData, 8, 0};
            std::array expected = {255, 0, 0, 255, 255, 255, 127, 127};

            testFormat(Format::RG16Sint, size, &subData, expected);
        }

        {
            int16_t texData[] = {255, 0, 127, 73};
            SubresourceData subData = {(void*)texData, 4, 0};
            std::array expected = {255, 0, 127, 73};

            testFormat(Format::R16Sint, size, &subData, expected);
        }

        {
            int8_t texData[] = {127, 0, 0, 127, 0, 127, 0, 127, 0, 0, 127, 127, 0, 0, 0, 127};
            SubresourceData subData = {(void*)texData, 8, 0};
            std::array expected = {127, 0, 0, 127, 0, 127, 0, 127, 0, 0, 127, 127, 0, 0, 0, 127};

            testFormat(Format::RGBA8Sint, size, &subData, expected);
        }

        {
            int8_t texData[] = {127, 0, 0, 127, 127, 127, 73, 73};
            SubresourceData subData = {(void*)texData, 4, 0};
            std::array expected = {127, 0, 0, 127, 127, 127, 73, 73};

            testFormat(Format::RG8Sint, size, &subData, expected);
        }

        {
            int8_t texData[] = {127, 0, 73, 25};
            SubresourceData subData = {(void*)texData, 2, 0};
            std::array expected = {127, 0, 73, 25};

            testFormat(Format::R8Sint, size, &subData, expected);
        }

        {
            // clang-format off
            uint16_t texData[] = {65535u, 0u, 0u, 65535u, 0u, 65535u, 0u, 65535u, 0u, 0u, 65535u, 65535u, 32767u, 32767u, 32767u, 32767u};
            SubresourceData subData = {(void*)texData, 16, 0};
            std::array expected = {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.499992371f, 0.499992371f, 0.499992371f, 0.499992371f};
            // clang-format on

            testFormat(Format::RGBA16Unorm, size, &subData, expected);
        }

        {
            uint16_t texData[] = {65535u, 0u, 0u, 65535u, 65535u, 65535u, 32767u, 32767u};
            SubresourceData subData = {(void*)texData, 8, 0};
            std::array expected = {1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.499992371f, 0.499992371f};

            testFormat(Format::RG16Unorm, size, &subData, expected);
        }

        {
            uint16_t texData[] = {65535u, 0u, 32767u, 16383u};
            SubresourceData subData = {(void*)texData, 4, 0};
            std::array expected = {1.0f, 0.0f, 0.499992371f, 0.249988556f};

            testFormat(Format::R16Unorm, size, &subData, expected);
        }

        {
            // clang-format off
            uint8_t texData[] = {0u, 0u, 0u, 255u, 127u, 127u, 127u, 255u, 255u, 255u, 255u, 255u, 0u, 0u, 0u, 0u};
            SubresourceData subData = {(void*)texData, 8, 0};
            std::array expected = {0.0f, 0.0f, 0.0f, 1.0f, 0.498039216f, 0.498039216f, 0.498039216f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f};
            std::array expectedSRGB = {0.0f, 0.0f, 0.0f, 1.0f, 0.211914062f, 0.211914062f, 0.211914062f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f};
            // clang-format on

            testFormat(Format::RGBA8Unorm, size, &subData, expected);
            testFormat(Format::RGBA8UnormSrgb, size, &subData, expectedSRGB);
        }

        {
            uint8_t texData[] = {255u, 0u, 0u, 255u, 255u, 255u, 127u, 127u};
            SubresourceData subData = {(void*)texData, 4, 0};
            std::array expected = {1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.498039216f, 0.498039216f};

            testFormat(Format::RG8Unorm, size, &subData, expected);
        }

        {
            uint8_t texData[] = {255u, 0u, 127u, 63u};
            SubresourceData subData = {(void*)texData, 2, 0};
            std::array expected = {1.0f, 0.0f, 0.498039216f, 0.247058824f};

            testFormat(Format::R8Unorm, size, &subData, expected);
        }

        {
            // clang-format off
            uint8_t texData[] = {0u, 0u, 0u, 255u, 127u, 127u, 127u, 255u, 255u, 255u, 255u, 255u, 0u, 0u, 0u, 0u};
            SubresourceData subData = {(void*)texData, 8, 0};
            std::array expected = {0.0f, 0.0f, 0.0f, 1.0f, 0.498039216f, 0.498039216f, 0.498039216f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f};
            std::array expectedSRGB = {0.0f, 0.0f, 0.0f, 1.0f, 0.211914062f, 0.211914062f, 0.211914062f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f};
            // clang-format on

            testFormat(Format::BGRA8Unorm, size, &subData, expected);
            testFormat(Format::BGRA8UnormSrgb, size, &subData, expectedSRGB);
        }

        {
            // clang-format off
            int16_t texData[] = {32767, 0, 0, 32767, 0, 32767, 0, 32767, 0, 0, 32767, 32767, -32768, -32768, 0, 32767};
            SubresourceData subData = {(void*)texData, 16, 0};
            std::array expected = {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, -1.0f, -1.0f, 0.0f, 1.0f};
            // clang-format on

            testFormat(Format::RGBA16Snorm, size, &subData, expected);
        }

        {
            int16_t texData[] = {32767, 0, 0, 32767, 32767, 32767, -32768, -32768};
            SubresourceData subData = {(void*)texData, 8, 0};
            std::array expected = {1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, -1.0f, -1.0f};

            testFormat(Format::RG16Snorm, size, &subData, expected);
        }

        {
            int16_t texData[] = {32767, 0, -32768, 0};
            SubresourceData subData = {(void*)texData, 4, 0};
            std::array expected = {1.0f, 0.0f, -1.0f, 0.0f};

            testFormat(Format::R16Snorm, size, &subData, expected);
        }

        {
            // clang-format off
            int8_t texData[] = {127, 0, 0, 127, 0, 127, 0, 127, 0, 0, 127, 127, -128, -128, 0, 127};
            SubresourceData subData = {(void*)texData, 8, 0};
            std::array expected = {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, -1.0f, -1.0f, 0.0f, 1.0f};
            // clang-format on

            testFormat(Format::RGBA8Snorm, size, &subData, expected);
        }

        {
            int8_t texData[] = {127, 0, 0, 127, 127, 127, -128, -128};
            SubresourceData subData = {(void*)texData, 4, 0};
            std::array expected = {1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, -1.0f, -1.0f};

            testFormat(Format::RG8Snorm, size, &subData, expected);
        }

        {
            int8_t texData[] = {127, 0, -128, 0};
            SubresourceData subData = {(void*)texData, 2, 0};
            std::array expected = {1.0f, 0.0f, -1.0f, 0.0f};

            testFormat(Format::R8Snorm, size, &subData, expected);
        }

        {
            // clang-format off
            uint8_t texData[] = {15u, 240u, 240u, 240u, 0u, 255u, 119u, 119u};
            SubresourceData subData = {(void*)texData, 4, 0};
            std::array expected = {0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.466666669f, 0.466666669f, 0.466666669f, 0.466666669f};
            // clang-format on

            testFormat(Format::BGRA4Unorm, size, &subData, expected);
        }

        {
            // clang-format off
            uint16_t texData[] = {31u, 2016u, 63488u, 31727u};
            SubresourceData subData = {(void*)texData, 4, 0};
            std::array expected = {0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.482352942f, 0.490196079f, 0.482352942f};
            // clang-format on

            testFormat(Format::B5G6R5Unorm, size, &subData, expected);
        }

        {
            // clang-format off
            uint16_t texData[] = {31u, 2016u, 63488u, 31727u};
            SubresourceData subData = {(void*)texData, 4, 0};
            std::array expected = {0.0f, 0.0f, 1.0f, 0.0f, 0.0313725509f, 1.0f, 0.0f, 0.0f, 0.968627453f, 0.0f, 0.0f, 1.0f, 0.968627453f, 1.0f, 0.482352942f, 0.0f};
            // clang-format on

            testFormat(Format::BGR5A1Unorm, size, &subData, expected);
        }

        {
            // clang-format off
            uint32_t texData[] = {2950951416u, 2013265920u, 3086219772u, 3087007228u};
            SubresourceData subData = {(void*)texData, 8, 0};
            std::array expected = {63.0f, 63.0f, 63.0f, 0.0f, 0.0f, 0.0f, 127.0f, 127.0f, 127.0f, 127.0f, 127.5f, 127.75f};
            // clang-format on

            testFormat(Format::RGB9E5Ufloat, size, &subData, expected);
        }

        {
            uint32_t texData[] = {4294967295u, 0u, 2683829759u, 1193046471u};
            SubresourceData subData = {(void*)texData, 8, 0};
            std::array expected = {1023u, 1023u, 1023u, 3u, 0u, 0u, 0u, 0u, 511u, 511u, 511u, 2u, 455u, 796u, 113u, 1u};

            testFormat(Format::RGB10A2Uint, size, &subData, expected);
        }

        {
            // clang-format off
            uint32_t texData[] = {4294967295u, 0u, 2683829759u, 1193046471u};
            SubresourceData subData = {(void*)texData, 8, 0};
            std::array expected = {1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.499511242f, 0.499511242f, 0.499511242f, 0.66666668f, 0.444770277f, 0.778103590f, 0.110459432f, 0.333333343f};
            // clang-format on

            testFormat(Format::RGB10A2Unorm, size, &subData, expected);
        }

        {
            uint32_t texData[] = {4294967295u, 0u, 2683829759u, 1193046471u};
            SubresourceData subData = {(void*)texData, 8, 0};
            std::array expected = {1023u, 1023u, 1023u, 3u, 0u, 0u, 0u, 0u, 511u, 511u, 511u, 2u, 455u, 796u, 113u, 1u};

            testFormat(Format::RGB10A2Uint, size, &subData, expected);
        }

        {
            uint32_t texData[] = {3085827519u, 0u, 2951478655u, 1880884096u};
            SubresourceData subData = {(void*)texData, 8, 0};
            std::array expected = {254.0f, 254.0f, 252.0f, 0.0f, 0.0f, 0.0f, 127.0f, 127.0f, 126.0f, 0.5f, 0.5f, 0.5f};

            testFormat(Format::R11G11B10Float, size, &subData, expected);
        }

#if 0
        {
            uint8_t texData[] = {255u, 255u, 255u, 255u, 255u, 255u, 255u, 255u};
            SubresourceData subData = {(void*)texData, 8, 0};
            std::array expected = {0.0f, 0.0f, 0.517647088f, 1.0f};
            std::array expectedSRGB = {0.0f, 0.0f, 0.230468750f, 1.0f};

            testFormat(Format::BC1Unorm, bcSize, &subData, expected);
            testFormat(Format::BC1UnormSrgb, bcSize, &subData, expectedSRGB);
        }
#endif

        {
            uint8_t texData[] = {255u, 255u, 255u, 255u, 255u, 255u, 255u, 255u, 16u, 0u, 0u, 0u, 0u, 0u, 0u, 0u};
            SubresourceData subData = {(void*)texData, 16, 0};
            std::array expected = {0.0f, 0.0f, 0.517647088f, 1.0f};
            std::array expectedSRGB = {0.0f, 0.0f, 0.230468750f, 1.0f};

            testFormat(Format::BC2Unorm, bcSize, &subData, expected);
            testFormat(Format::BC2UnormSrgb, bcSize, &subData, expectedSRGB);
        }

        {
            uint8_t texData[] = {0u, 255u, 255u, 255u, 255u, 255u, 255u, 255u, 16u, 0u, 0u, 0u, 0u, 0u, 0u, 0u};
            SubresourceData subData = {(void*)texData, 16, 0};
            std::array expected = {0.0f, 0.0f, 0.517647088f, 1.0f};
            std::array expectedSRGB = {0.0f, 0.0f, 0.230468750f, 1.0f};

            testFormat(Format::BC3Unorm, bcSize, &subData, expected);
            testFormat(Format::BC3UnormSrgb, bcSize, &subData, expectedSRGB);
        }

        {
            uint8_t texData[] = {127u, 0u, 0u, 0u, 0u, 0u, 0u, 0u};
            SubresourceData subData = {(void*)texData, 8, 0};
            std::array expectedUNORM = {0.498039216f};
            std::array expectedSNORM = {1.0f};

            testFormat(Format::BC4Unorm, bcSize, &subData, expectedUNORM);
            testFormat(Format::BC4Snorm, bcSize, &subData, expectedSNORM);
        }

        {
            uint8_t texData[] = {127u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 127u, 0u, 0u, 0u, 0u, 0u, 0u, 0u};
            SubresourceData subData = {(void*)texData, 16, 0};
            std::array expectedUNORM = {0.498039216f, 0.498039216f, 0.498039216f, 0.498039216f};
            std::array expectedSNORM = {1.0f, 1.0f, 1.0f, 1.0f};

            testFormat(Format::BC5Unorm, bcSize, &subData, expectedUNORM);
            testFormat(Format::BC5Snorm, bcSize, &subData, expectedSNORM);
        }

        {
            // clang-format off
            uint8_t texData[] = {98u, 238u, 232u, 77u, 240u, 66u, 148u, 31u, 124u, 95u, 2u, 224u, 255u, 107u, 77u, 250u};
            SubresourceData subData = {(void*)texData, 16, 0};
            std::array expected = {0.343261719f, 0.897949219f, 2.16406250f};
            // clang-format on

            testFormat(Format::BC6HUfloat, bcSize, &subData, expected);
        }

        {
            // clang-format off
            uint8_t texData[] = {107u, 238u, 232u, 77u, 240u, 71u, 128u, 127u, 1u, 0u, 255u, 255u, 170u, 218u, 221u, 254u};
            SubresourceData subData = {(void*)texData, 16, 0};
            std::array expected = {0.343261719f, 0.897949219f, 2.16406250f};
            // clang-format on

            testFormat(Format::BC6HSfloat, bcSize, &subData, expected);
        }

        {
            uint8_t texData[] = {104u, 0u, 0u, 0u, 64u, 163u, 209u, 104u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u};
            SubresourceData subData = {(void*)texData, 16, 0};
            std::array expected = {0.0f, 0.101960786f, 0.0f, 1.0f};
            std::array expectedSRGB = {0.0f, 0.0103149414f, 0.0f, 1.0f};

            testFormat(Format::BC7Unorm, bcSize, &subData, expected);
            testFormat(Format::BC7UnormSrgb, bcSize, &subData, expectedSRGB);
        }
    }
};

// skip CPU: Vector types not implemented
GPU_TEST_CASE("formats", ALL & ~CPU)
{
    TestFormats test;
    test.init(device);
    test.run();
}
