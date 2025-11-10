#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

GPU_TEST_CASE("shader-object-resource-tracking", ALL & ~CPU)
{
    ComPtr<IShaderProgram> shaderProgram;
    REQUIRE_CALL(loadProgram(device, "test-shader-object-resource-tracking", "computeMain", shaderProgram.writeRef()));

    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

    ComPtr<IBuffer> globalBuffer;
    {
        BufferDesc bufferDesc = {};
        bufferDesc.size = sizeof(float);
        bufferDesc.usage = BufferUsage::CopyDestination | BufferUsage::ShaderResource;
        float initialData[] = {10.f};
        REQUIRE_CALL(device->createBuffer(bufferDesc, initialData, globalBuffer.writeRef()));
    }

    ComPtr<ITexture> globalTexture;
    {
        TextureDesc textureDesc = {};
        textureDesc.size = {2, 2, 1};
        textureDesc.format = Format::R32Float;
        textureDesc.usage = TextureUsage::CopyDestination | TextureUsage::ShaderResource;
        float initialData[] = {1.f, 2.f, 3.f, 4.f};
        SubresourceData subresourceData[] = {{initialData, sizeof(float) * 2, 0}};
        REQUIRE_CALL(device->createTexture(textureDesc, subresourceData, globalTexture.writeRef()));
    }

    ComPtr<ISampler> globalSampler;
    {
        SamplerDesc samplerDesc = {};
        samplerDesc.minFilter = TextureFilteringMode::Point;
        samplerDesc.magFilter = TextureFilteringMode::Point;
        REQUIRE_CALL(device->createSampler(samplerDesc, globalSampler.writeRef()));
    }

    ComPtr<IBuffer> buffer;
    {
        BufferDesc bufferDesc = {};
        bufferDesc.size = sizeof(float);
        bufferDesc.usage = BufferUsage::CopyDestination | BufferUsage::ShaderResource;
        float initialData[] = {20.f};
        REQUIRE_CALL(device->createBuffer(bufferDesc, initialData, buffer.writeRef()));
    }

    ComPtr<ITexture> texture;
    {
        TextureDesc textureDesc = {};
        textureDesc.size = {2, 2, 1};
        textureDesc.format = Format::R32Float;
        textureDesc.usage = TextureUsage::CopyDestination | TextureUsage::ShaderResource;
        float initialData[] = {1.f, 2.f, 3.f, 4.f};
        SubresourceData subresourceData[] = {{initialData, sizeof(float) * 2, 0}};
        REQUIRE_CALL(device->createTexture(textureDesc, subresourceData, texture.writeRef()));
    }

    ComPtr<ISampler> sampler;
    {
        SamplerDesc samplerDesc = {};
        samplerDesc.minFilter = TextureFilteringMode::Linear;
        samplerDesc.magFilter = TextureFilteringMode::Linear;
        REQUIRE_CALL(device->createSampler(samplerDesc, sampler.writeRef()));
    }

    ComPtr<IBuffer> resultBuffer;
    {
        BufferDesc bufferDesc = {};
        bufferDesc.size = 4 * sizeof(float);
        bufferDesc.usage = BufferUsage::CopySource | BufferUsage::UnorderedAccess;
        REQUIRE_CALL(device->createBuffer(bufferDesc, nullptr, resultBuffer.writeRef()));
    }

    {
        auto queue = device->getQueue(QueueType::Graphics);
        auto commandEncoder = queue->createCommandEncoder();

        auto passEncoder = commandEncoder->beginComputePass();
        auto rootObject = passEncoder->bindPipeline(pipeline);
        ShaderCursor globalsCursor(rootObject);
        globalsCursor["globalBuffer"].setBinding(globalBuffer);
        globalsCursor["globalTexture"].setBinding(globalTexture);
        globalsCursor["globalSampler"].setBinding(globalSampler);
        ShaderCursor entryPointCursor(rootObject->getEntryPoint(0));
        entryPointCursor["buffer"].setBinding(buffer);
        entryPointCursor["texture"].setBinding(texture);
        entryPointCursor["sampler"].setBinding(sampler);
        entryPointCursor["resultBuffer"].setBinding(resultBuffer);

        // At this point, the shader object should keep all resources alive.
        // We can release them and they should not be destroyed until the command
        // encoder is finished and the command buffer is executed.
        globalBuffer.setNull();
        globalTexture.setNull();
        globalSampler.setNull();
        buffer.setNull();
        texture.setNull();
        sampler.setNull();

        passEncoder->dispatchCompute(1, 1, 1);
        passEncoder->end();

        // Finish command encoding and release the command encoder,
        // making sure it does not keep any resources alive, but the command buffer
        // still does.
        ComPtr<ICommandBuffer> commandBuffer = commandEncoder->finish();
        commandEncoder.setNull();

        queue->submit(commandBuffer);
        queue->waitOnHost();
    }

    compareComputeResult(device, resultBuffer, makeArray<float>(10.f, 1.f, 20.f, 2.5f));
}
