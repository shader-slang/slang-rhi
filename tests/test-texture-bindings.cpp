#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

namespace {

ComPtr<IComputePipeline> createPipeline(IDevice* device, const char* entryPoint)
{
    ComPtr<IShaderProgram> program;
    REQUIRE_CALL(loadProgram(device, "test-texture-bindings", entryPoint, program.writeRef()));
    ComputePipelineDesc desc = {};
    desc.program = program;
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(desc, pipeline.writeRef()));
    return pipeline;
}

ComPtr<IBuffer> createResults(IDevice* device, size_t count)
{
    BufferDesc desc = {};
    desc.size = count * sizeof(float);
    desc.elementSize = 4 * sizeof(float);
    desc.usage = BufferUsage::UnorderedAccess | BufferUsage::CopySource;
    auto buffer = device->createBuffer(desc);
    REQUIRE(buffer);
    return buffer;
}

void checkResults(IDevice* device, IBuffer* buffer, std::span<const float> expected)
{
    ComPtr<ISlangBlob> data;
    REQUIRE_CALL(device->readBuffer(buffer, 0, expected.size_bytes(), data.writeRef()));
    const float* values = static_cast<const float*>(data->getBufferPointer());
    for (size_t i = 0; i < expected.size(); ++i)
    {
        CAPTURE(i);
        CHECK(values[i] == expected[i]);
    }
}

} // namespace

GPU_TEST_CASE("texture-storage-bindings", D3D11 | D3D12 | Vulkan | Metal | WGPU)
{
    auto writePipeline = createPipeline(device, "writeTextures");
    auto readPipeline = createPipeline(device, "readTextures");
    TextureDesc desc = {};
    desc.size = {1, 1, 1};
    desc.format = Format::RGBA16Float;
    desc.usage = TextureUsage::UnorderedAccess | TextureUsage::ShaderResource;
    auto hdr = device->createTexture(desc);
    REQUIRE(hdr);
    desc.type = TextureType::Texture3D;
    desc.size.depth = 2;
    desc.format = Format::RGBA8Unorm;
    auto volume = device->createTexture(desc);
    REQUIRE(volume);
    desc.type = TextureType::Texture2D;
    desc.size.depth = 1;
    desc.format = Format::R32Uint;
    desc.usage |= TextureUsage::CopyDestination;
    uint32_t initialCounter = 7;
    SubresourceData initialData = {&initialCounter, sizeof(initialCounter), sizeof(initialCounter)};
    auto counter = device->createTexture(desc, &initialData);
    REQUIRE(counter);
    auto results = createResults(device, 12);

    auto queue = device->getQueue(QueueType::Graphics);
    auto encoder = queue->createCommandEncoder();
    auto pass = encoder->beginComputePass();
    ShaderCursor write(pass->bindPipeline(writePipeline));
    REQUIRE_CALL(write["storage"]["hdr"].setBinding(hdr));
    REQUIRE_CALL(write["storage"]["volume"].setBinding(volume));
    REQUIRE_CALL(write["storage"]["counter"].setBinding(counter));
    pass->dispatchCompute(1, 1, 1);
    pass->end();
    pass = encoder->beginComputePass();
    ShaderCursor read(pass->bindPipeline(readPipeline));
    REQUIRE_CALL(read["hdrInput"].setBinding(hdr));
    REQUIRE_CALL(read["volumeInput"].setBinding(volume));
    REQUIRE_CALL(read["counterInput"].setBinding(counter));
    REQUIRE_CALL(read["results"].setBinding(results));
    pass->dispatchCompute(1, 1, 1);
    pass->end();
    REQUIRE_CALL(queue->submit(encoder->finish()));
    REQUIRE_CALL(queue->waitOnHost());
    const float expected[] = {0.25f, 0.5f, 2.f, 1.f, 0.f, 1.f, 0.f, 1.f, 10.f, 0.f, 0.f, 0.f};
    checkResults(device, results, expected);
}

GPU_TEST_CASE("texture-integer-bindings", D3D11 | D3D12 | Vulkan | Metal | WGPU)
{
    auto pipeline = createPipeline(device, "readIntegers");
    TextureDesc desc = {};
    desc.size = {1, 1, 1};
    desc.format = Format::RGBA8Uint;
    desc.usage = TextureUsage::ShaderResource | TextureUsage::CopyDestination;
    uint8_t unsignedValues[] = {1, 2, 3, 4};
    SubresourceData initialData = {unsignedValues, sizeof(unsignedValues), sizeof(unsignedValues)};
    auto unsignedTexture = device->createTexture(desc, &initialData);
    REQUIRE(unsignedTexture);
    desc.format = Format::RGBA8Sint;
    int8_t signedValues[] = {-1, -2, 3, -4};
    initialData.data = signedValues;
    auto signedTexture = device->createTexture(desc, &initialData);
    REQUIRE(signedTexture);
    auto results = createResults(device, 8);

    auto queue = device->getQueue(QueueType::Graphics);
    auto encoder = queue->createCommandEncoder();
    auto pass = encoder->beginComputePass();
    ShaderCursor cursor(pass->bindPipeline(pipeline));
    REQUIRE_CALL(cursor["uintInput"].setBinding(unsignedTexture));
    REQUIRE_CALL(cursor["intInput"].setBinding(signedTexture));
    REQUIRE_CALL(cursor["results"].setBinding(results));
    pass->dispatchCompute(1, 1, 1);
    pass->end();
    REQUIRE_CALL(queue->submit(encoder->finish()));
    REQUIRE_CALL(queue->waitOnHost());
    const float expected[] = {1.f, 2.f, 3.f, 4.f, -1.f, -2.f, 3.f, -4.f};
    checkResults(device, results, expected);
}
