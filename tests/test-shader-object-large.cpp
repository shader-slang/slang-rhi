#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

inline ComPtr<IBuffer> createBuffer(IDevice* device, float value)
{
    BufferDesc bufferDesc = {};
    bufferDesc.size = sizeof(float);
    bufferDesc.usage = BufferUsage::CopyDestination | BufferUsage::ShaderResource;
    float initialData[] = {value};
    ComPtr<IBuffer> buffer;
    REQUIRE_CALL(device->createBuffer(bufferDesc, initialData, buffer.writeRef()));
    return buffer;
}

inline ComPtr<ITexture> createTexture(IDevice* device, float value)
{
    TextureDesc textureDesc = {};
    textureDesc.size = {1, 1, 1};
    textureDesc.format = Format::R32Float;
    textureDesc.usage = TextureUsage::CopyDestination | TextureUsage::ShaderResource;
    float initialData[] = {value};
    SubresourceData subresourceData[] = {{initialData, sizeof(float), 0}};
    ComPtr<ITexture> texture;
    REQUIRE_CALL(device->createTexture(textureDesc, subresourceData, texture.writeRef()));
    return texture;
}

GPU_TEST_CASE("shader-object-large", D3D12 | Vulkan)
{
    ComPtr<IShaderProgram> shaderProgram;
    REQUIRE_CALL(loadProgram(device, "test-shader-object-large", "computeMain", shaderProgram.writeRef()));

    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

    std::vector<ComPtr<IBuffer>> globalBuffers;
    std::vector<ComPtr<ITexture>> globalTextures;
    std::vector<ComPtr<IBuffer>> pbBuffers;
    std::vector<ComPtr<ITexture>> pbTextures;
    std::vector<ComPtr<IBuffer>> localBuffers;
    std::vector<ComPtr<ITexture>> localTextures;

    static constexpr int N = 1024;

    std::vector<float> expectedResult;

    for (int i = 0; i < N; ++i)
    {
        float value = i;
        globalBuffers.push_back(createBuffer(device, value));
        expectedResult.push_back(value);

        value = i + 1000;
        globalTextures.push_back(createTexture(device, value));
        expectedResult.push_back(value);

        value = i * 2;
        pbBuffers.push_back(createBuffer(device, value));
        expectedResult.push_back(value);

        value = i * 2 + 1000;
        pbTextures.push_back(createTexture(device, value));
        expectedResult.push_back(value);

        value = i * 3;
        localBuffers.push_back(createBuffer(device, value));
        expectedResult.push_back(value);

        value = i * 3 + 1000;
        localTextures.push_back(createTexture(device, value));
        expectedResult.push_back(value);
    }

    ComPtr<IBuffer> resultBuffer;
    {
        BufferDesc bufferDesc = {};
        bufferDesc.size = N * 6 * sizeof(float);
        bufferDesc.usage = BufferUsage::CopySource | BufferUsage::UnorderedAccess;
        REQUIRE_CALL(device->createBuffer(bufferDesc, nullptr, resultBuffer.writeRef()));
    }

    static constexpr int kIterations = 10;

    for (int it = 0; it < kIterations; ++it)
    {
        auto queue = device->getQueue(QueueType::Graphics);
        auto commandEncoder = queue->createCommandEncoder();

        auto passEncoder = commandEncoder->beginComputePass();
        auto rootObject = passEncoder->bindPipeline(pipeline);
        ShaderCursor globalsCursor(rootObject);
        ShaderCursor pbCursor(globalsCursor["pb"]);
        ShaderCursor entryPointCursor(rootObject->getEntryPoint(0));
        for (int i = 0; i < N; ++i)
        {
            globalsCursor["globalBuffers"][i].setBinding(globalBuffers[i]);
            globalsCursor["globalTextures"][i].setBinding(globalTextures[i]);
            pbCursor["buffers"][i].setBinding(pbBuffers[i]);
            pbCursor["textures"][i].setBinding(pbTextures[i]);
            entryPointCursor["localBuffers"][i].setBinding(localBuffers[i]);
            entryPointCursor["localTextures"][i].setBinding(localTextures[i]);
        }
        entryPointCursor["resultBuffer"].setBinding(resultBuffer);
        passEncoder->dispatchCompute(N, 1, 1);
        passEncoder->end();
        queue->submit(commandEncoder->finish());
        queue->waitOnHost();

        compareComputeResult(device, resultBuffer, span(expectedResult));
    }
}
