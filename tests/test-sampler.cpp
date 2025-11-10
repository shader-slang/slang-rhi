#include "testing.h"
#include "core/span.h"

using namespace rhi;
using namespace rhi::testing;

static Result createTestTexture(IDevice* device, ITexture** outTexture)
{
    ComPtr<ITexture> texture;
    TextureDesc desc = {};
    desc.type = TextureType::Texture2D;
    desc.format = Format::RGBA32Float;
    desc.size = {2, 2, 1};
    desc.mipCount = 2;
    desc.memoryType = MemoryType::DeviceLocal;
    desc.usage = TextureUsage::ShaderResource | TextureUsage::CopyDestination | TextureUsage::CopySource;

    // mip 0
    // ---------------------
    // |         |         |
    // | 1,0,0,0 | 0,1,0,0 |
    // |         |         |
    // ---------------------
    // |         |         |
    // | 0,0,1,0 | 0,0,0,1 |
    // |         |         |
    // ---------------------
    // mip 1
    // -----------
    // |         |
    // | 1,1,1,1 |
    // |         |
    // -----------

    float mip0Data[] = {1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f};
    float mip1Data[] = {1.f, 1.f, 1.f, 1.f};
    SubresourceData subResourceData[2] = {
        {mip0Data, 32, 0},
        {mip1Data, 16, 0},
    };
    return device->createTexture(desc, subResourceData, outTexture);
}

struct TestInput
{
    float u;
    float v;
    float level;
    float padding;
};

struct TestOutput
{
    float color[4];
};

struct TestRecord
{
    float u;
    float v;
    float level;
    float expectedColor[4];
};

struct SamplerTest
{
    static constexpr size_t kMaxRecords = 32;

    ComPtr<IDevice> device;
    ComPtr<ITexture> texture;
    ComPtr<IBuffer> inputBuffer;
    ComPtr<IBuffer> resultBuffer;
    ComPtr<IComputePipeline> pipeline;

    void init(IDevice* device_)
    {
        this->device = device_;
        REQUIRE_CALL(createTestTexture(device, texture.writeRef()));

        ComPtr<IShaderProgram> shaderProgram;
        REQUIRE_CALL(loadProgram(device, "test-sampler", "sampleTexture", shaderProgram.writeRef()));

        ComputePipelineDesc pipelineDesc = {};
        pipelineDesc.program = shaderProgram.get();
        REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

        BufferDesc bufferDesc = {};
        bufferDesc.size = kMaxRecords * sizeof(TestInput);
        bufferDesc.elementSize = sizeof(TestInput);
        bufferDesc.usage = BufferUsage::ShaderResource | BufferUsage::CopyDestination;
        REQUIRE_CALL(device->createBuffer(bufferDesc, nullptr, inputBuffer.writeRef()));

        bufferDesc.size = kMaxRecords * sizeof(TestOutput);
        bufferDesc.elementSize = sizeof(TestOutput);
        bufferDesc.usage = BufferUsage::UnorderedAccess | BufferUsage::CopySource;
        REQUIRE_CALL(device->createBuffer(bufferDesc, nullptr, resultBuffer.writeRef()));
    }

    void check(ISampler* sampler, span<TestRecord> testRecords)
    {
        REQUIRE(testRecords.size() <= kMaxRecords);
        std::vector<TestInput> inputData;
        for (const auto& record : testRecords)
        {
            inputData.push_back({record.u, record.v, record.level, 0.f});
        }
        ComPtr<ICommandQueue> queue = device->getQueue(QueueType::Graphics);
        ComPtr<ICommandEncoder> encoder = queue->createCommandEncoder();
        encoder->uploadBufferData(inputBuffer, 0, inputData.size() * sizeof(TestInput), inputData.data());
        IComputePassEncoder* passEncoder = encoder->beginComputePass();
        IShaderObject* rootObject = passEncoder->bindPipeline(pipeline);
        ShaderCursor cursor(rootObject);
        cursor["texture"].setBinding(texture);
        cursor["sampler"].setBinding(sampler);
        cursor["inputs"].setBinding(inputBuffer);
        cursor["results"].setBinding(resultBuffer);
        cursor["count"].setData((uint32_t)testRecords.size());
        passEncoder->dispatchCompute((uint32_t)testRecords.size(), 1, 1);
        passEncoder->end();
        queue->submit(encoder->finish());
        queue->waitOnHost();

        ComPtr<ISlangBlob> resultData;
        REQUIRE_CALL(
            device->readBuffer(resultBuffer, 0, testRecords.size() * sizeof(TestOutput), resultData.writeRef())
        );
        const TestOutput* output = (const TestOutput*)resultData->getBufferPointer();
        for (size_t i = 0; i < testRecords.size(); i++)
        {
            const TestRecord& record = testRecords[i];
            CAPTURE(record.u);
            CAPTURE(record.v);
            CAPTURE(record.level);
            for (size_t j = 0; j < 4; j++)
            {
                CAPTURE(j);
                REQUIRE_EQ(output[i].color[j], testRecords[i].expectedColor[j]);
            }
        }
    }
};

static void testSampler(IDevice* device, const SamplerDesc& samplerDesc, span<TestRecord> testRecords)
{
    ComPtr<ISampler> sampler;
    REQUIRE_CALL(device->createSampler(samplerDesc, sampler.writeRef()));

    SamplerTest test;
    test.init(device);
    test.check(sampler, testRecords);
}

GPU_TEST_CASE("sampler-filter-point", D3D11 | D3D12 | Vulkan | Metal | WGPU)
{
    SamplerDesc desc = {};
    desc.minFilter = TextureFilteringMode::Point;
    desc.magFilter = TextureFilteringMode::Point;

    TestRecord testRecords[] = {
        // top-left texel
        {0.01f, 0.01f, 0.f, {1.f, 0.f, 0.f, 0.f}},
        {0.25f, 0.25f, 0.f, {1.f, 0.f, 0.f, 0.f}},
        {0.49f, 0.49f, 0.f, {1.f, 0.f, 0.f, 0.f}},
        // top-right texel
        {0.51f, 0.01f, 0.f, {0.f, 1.f, 0.f, 0.f}},
        {0.75f, 0.25f, 0.f, {0.f, 1.f, 0.f, 0.f}},
        {0.99f, 0.49f, 0.f, {0.f, 1.f, 0.f, 0.f}},
        // bottom-left texel
        {0.01f, 0.51f, 0.f, {0.f, 0.f, 1.f, 0.f}},
        {0.25f, 0.75f, 0.f, {0.f, 0.f, 1.f, 0.f}},
        {0.49f, 0.99f, 0.f, {0.f, 0.f, 1.f, 0.f}},
        // bottom-right texel
        {0.51f, 0.51f, 0.f, {0.f, 0.f, 0.f, 1.f}},
        {0.75f, 0.75f, 0.f, {0.f, 0.f, 0.f, 1.f}},
        {0.99f, 0.99f, 0.f, {0.f, 0.f, 0.f, 1.f}},
    };

    testSampler(device, desc, testRecords);
}

GPU_TEST_CASE("sampler-filter-linear", D3D11 | D3D12 | Vulkan | Metal | WGPU)
{
    SamplerDesc desc = {};
    desc.minFilter = TextureFilteringMode::Linear;
    desc.magFilter = TextureFilteringMode::Linear;

    TestRecord testRecords[] = {
        // top-left texel
        {0.25f, 0.25f, 0.f, {1.f, 0.f, 0.f, 0.f}},
        // top-right texel
        {0.75f, 0.25f, 0.f, {0.f, 1.f, 0.f, 0.f}},
        // bottom-left texel
        {0.25f, 0.75f, 0.f, {0.f, 0.f, 1.f, 0.f}},
        // bottom-right texel
        {0.75f, 0.75f, 0.f, {0.f, 0.f, 0.f, 1.f}},
        // left (interpolated)
        {0.25f, 0.5f, 0.f, {0.5f, 0.f, 0.5f, 0.f}},
        // right (interpolated)
        {0.75f, 0.5f, 0.f, {0.f, 0.5f, 0.f, 0.5f}},
        // top (interpolated)
        {0.5f, 0.25f, 0.f, {0.5f, 0.5f, 0.f, 0.f}},
        // bottom (interpolated)
        {0.5f, 0.75f, 0.f, {0.f, 0.f, 0.5f, 0.5f}},
        // middle (interpolated)
        {0.5f, 0.5f, 0.f, {0.25f, 0.25f, 0.25f, 0.25f}},
    };

    testSampler(device, desc, testRecords);
}

GPU_TEST_CASE("sampler-border-black-transparent", D3D11 | D3D12 | Vulkan | Metal)
{
    SamplerDesc desc = {};
    desc.addressU = TextureAddressingMode::ClampToBorder;
    desc.addressV = TextureAddressingMode::ClampToBorder;
    desc.addressW = TextureAddressingMode::ClampToBorder;

    TestRecord testRecords[] = {
        // outside of texture
        {-0.5f, -0.5f, 0.f, {0.f, 0.f, 0.f, 0.f}},
    };

    testSampler(device, desc, testRecords);
}

GPU_TEST_CASE("sampler-border-black-opaque", D3D11 | D3D12 | Vulkan | Metal)
{
    SamplerDesc desc = {};
    desc.addressU = TextureAddressingMode::ClampToBorder;
    desc.addressV = TextureAddressingMode::ClampToBorder;
    desc.addressW = TextureAddressingMode::ClampToBorder;
    desc.borderColor[0] = 0.f;
    desc.borderColor[1] = 0.f;
    desc.borderColor[2] = 0.f;
    desc.borderColor[3] = 1.f;


    TestRecord testRecords[] = {
        // outside of texture
        {-0.5f, -0.5f, 0.f, {0.f, 0.f, 0.f, 1.f}},
    };

    testSampler(device, desc, testRecords);
}

GPU_TEST_CASE("sampler-border-white-opaque", D3D11 | D3D12 | Vulkan | Metal)
{
    SamplerDesc desc = {};
    desc.addressU = TextureAddressingMode::ClampToBorder;
    desc.addressV = TextureAddressingMode::ClampToBorder;
    desc.addressW = TextureAddressingMode::ClampToBorder;
    desc.borderColor[0] = 1.f;
    desc.borderColor[1] = 1.f;
    desc.borderColor[2] = 1.f;
    desc.borderColor[3] = 1.f;

    TestRecord testRecords[] = {
        // outside of texture
        {-0.5f, -0.5f, 0.f, {1.f, 1.f, 1.f, 1.f}},
    };

    testSampler(device, desc, testRecords);
}

GPU_TEST_CASE("sampler-border-custom-color", D3D11 | D3D12 | Vulkan | Metal)
{
    if (!device->hasFeature(Feature::CustomBorderColor))
        SKIP("Custom border color not supported");

    SamplerDesc desc = {};
    desc.addressU = TextureAddressingMode::ClampToBorder;
    desc.addressV = TextureAddressingMode::ClampToBorder;
    desc.addressW = TextureAddressingMode::ClampToBorder;
    desc.borderColor[0] = 0.25f;
    desc.borderColor[1] = 0.5f;
    desc.borderColor[2] = 0.75f;
    desc.borderColor[3] = 1.f;

    TestRecord testRecords[] = {
        // outside of texture
        {-0.5f, -0.5f, 0.f, {0.25f, 0.5f, 0.75f, 1.f}},
    };

    testSampler(device, desc, testRecords);
}
