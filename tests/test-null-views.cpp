#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

// skip D3D11: too many UAVs
// skip CPU: invalid results
// skip WGPU: null views don't exist, would need to create dummy resources
GPU_TEST_CASE("null-views", ALL & ~(D3D11 | CPU | WGPU))
{
    ComPtr<IShaderProgram> shaderProgram;
    REQUIRE_CALL(loadProgram(device, "test-null-views", "computeMain", shaderProgram.writeRef()));

    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

    ComPtr<IBuffer> buffer;
    {
        BufferDesc desc = {};
        desc.size = 4;
        desc.format = Format::R32Float;
        desc.usage = BufferUsage::ShaderResource;
        float data = 1.f;
        REQUIRE_CALL(device->createBuffer(desc, &data, buffer.writeRef()));
    }

    ComPtr<IBuffer> rwBuffer;
    {
        BufferDesc desc = {};
        desc.size = 4;
        desc.format = Format::R32Float;
        desc.usage = BufferUsage::UnorderedAccess;
        float data = 2.f;
        REQUIRE_CALL(device->createBuffer(desc, &data, rwBuffer.writeRef()));
    }

    ComPtr<IBuffer> structuredBuffer;
    {
        BufferDesc desc = {};
        desc.size = 4;
        desc.usage = BufferUsage::ShaderResource;
        float data = 3.f;
        REQUIRE_CALL(device->createBuffer(desc, &data, structuredBuffer.writeRef()));
    }

    ComPtr<IBuffer> rwStructuredBuffer;
    {
        BufferDesc desc = {};
        desc.size = 4;
        desc.usage = BufferUsage::UnorderedAccess;
        float data = 4.f;
        REQUIRE_CALL(device->createBuffer(desc, &data, rwStructuredBuffer.writeRef()));
    }

    ComPtr<ITexture> texture;
    {
        TextureDesc desc = {};
        desc.type = TextureType::Texture2D;
        desc.size = {1, 1, 1};
        desc.format = Format::R32Float;
        desc.usage = TextureUsage::ShaderResource;
        float data = 5.f;
        SubresourceData subresourceData[] = {{&data, 4, 0}};
        REQUIRE_CALL(device->createTexture(desc, subresourceData, texture.writeRef()));
    }

    ComPtr<ITexture> rwTexture;
    {
        TextureDesc desc = {};
        desc.type = TextureType::Texture2D;
        desc.size = {1, 1, 1};
        desc.format = Format::R32Float;
        desc.usage = TextureUsage::UnorderedAccess;
        float data = 6.f;
        SubresourceData subresourceData[] = {{&data, 4, 0}};
        REQUIRE_CALL(device->createTexture(desc, subresourceData, rwTexture.writeRef()));
    }

    ComPtr<ITexture> textureArray;
    {
        TextureDesc desc = {};
        desc.type = TextureType::Texture2DArray;
        desc.size = {1, 1, 1};
        desc.arrayLength = 2;
        desc.format = Format::R32Float;
        desc.usage = TextureUsage::ShaderResource;
        float data = 7.f;
        SubresourceData subresourceData[] = {{&data, 4, 0}, {&data, 4, 0}};
        REQUIRE_CALL(device->createTexture(desc, subresourceData, textureArray.writeRef()));
    }

    ComPtr<ITexture> rwTextureArray;
    {
        TextureDesc desc = {};
        desc.type = TextureType::Texture2DArray;
        desc.size = {1, 1, 1};
        desc.arrayLength = 2;
        desc.format = Format::R32Float;
        desc.usage = TextureUsage::UnorderedAccess;
        float data = 8.f;
        SubresourceData subresourceData[] = {{&data, 4, 0}, {&data, 4, 0}};
        REQUIRE_CALL(device->createTexture(desc, subresourceData, rwTextureArray.writeRef()));
    }

    ComPtr<ISampler> sampler;
    {
        SamplerDesc desc = {};
        REQUIRE_CALL(device->createSampler(desc, sampler.writeRef()));
    }

    ComPtr<IBuffer> result;
    {
        BufferDesc desc = {};
        desc.size = 64;
        desc.usage = BufferUsage::UnorderedAccess | BufferUsage::CopySource;
        REQUIRE_CALL(device->createBuffer(desc, nullptr, result.writeRef()));
    }

    {
        auto queue = device->getQueue(QueueType::Graphics);
        auto commandEncoder = queue->createCommandEncoder();
        auto passEncoder = commandEncoder->beginComputePass();
        IShaderObject* rootObject = passEncoder->bindPipeline(pipeline);
        ShaderCursor cursor(rootObject);
        REQUIRE_CALL(cursor["buffer1"].setBinding(static_cast<IBuffer*>(nullptr)));
        REQUIRE_CALL(cursor["buffer2"].setBinding(buffer));
        // "buffer3" not set explicitly
        REQUIRE_CALL(cursor["rwBuffer1"].setBinding(static_cast<IBuffer*>(nullptr)));
        REQUIRE_CALL(cursor["rwBuffer2"].setBinding(rwBuffer));
        // "rwBuffer3" not set explicitly
        REQUIRE_CALL(cursor["structuredBuffer1"].setBinding(static_cast<IBuffer*>(nullptr)));
        REQUIRE_CALL(cursor["structuredBuffer2"].setBinding(structuredBuffer));
        // "structuredBuffer3" not set explicitly
        REQUIRE_CALL(cursor["rwStructuredBuffer1"].setBinding(static_cast<IBuffer*>(nullptr)));
        REQUIRE_CALL(cursor["rwStructuredBuffer2"].setBinding(rwStructuredBuffer));
        // "rwStructuredBuffer3" not set explicitly
        REQUIRE_CALL(cursor["texture1"].setBinding(static_cast<ITexture*>(nullptr)));
        REQUIRE_CALL(cursor["texture2"].setBinding(texture));
        // "texture3" not set explicitly
        REQUIRE_CALL(cursor["rwTexture1"].setBinding(static_cast<ITexture*>(nullptr)));
        REQUIRE_CALL(cursor["rwTexture2"].setBinding(rwTexture));
        // "rwTexture3" not set explicitly
        REQUIRE_CALL(cursor["textureArray1"].setBinding(static_cast<ITexture*>(nullptr)));
        REQUIRE_CALL(cursor["textureArray2"].setBinding(textureArray));
        // "textureArray3" not set explicitly
        REQUIRE_CALL(cursor["rwTextureArray1"].setBinding(static_cast<ITexture*>(nullptr)));
        REQUIRE_CALL(cursor["rwTextureArray2"].setBinding(rwTextureArray));
        // "rwTextureArray3" not set explicitly
        REQUIRE_CALL(cursor["samplerState1"].setBinding(static_cast<ISampler*>(nullptr)));
        REQUIRE_CALL(cursor["samplerState2"].setBinding(sampler));
        // "samplerState3" not set explicitly
        REQUIRE_CALL(cursor["result"].setBinding(result));

        passEncoder->dispatchCompute(1, 1, 1);
        passEncoder->end();

        queue->submit(commandEncoder->finish());
        queue->waitOnHost();
    }

    compareComputeResult(device, result, std::array{1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 7.f, 8.f, 5.f});
}
