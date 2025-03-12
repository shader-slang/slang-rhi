#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

// skip D3D11: too many UAVs
// skip CPU: invalid results
// skip WGPU: null views don't exist, would need to create dummy resources
GPU_TEST_CASE("null-views", ALL & ~(D3D11 | CPU | WGPU))
{
    ComPtr<IShaderProgram> shaderProgram;
    slang::ProgramLayout* slangReflection;
    REQUIRE_CALL(loadComputeProgram(device, shaderProgram, "test-null-views", "compute_main", slangReflection));

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
        cursor["buffer2"].setBinding(buffer);
        cursor["rwBuffer2"].setBinding(rwBuffer);
        cursor["structuredBuffer2"].setBinding(structuredBuffer);
        cursor["rwStructuredBuffer2"].setBinding(rwStructuredBuffer);
        cursor["texture2"].setBinding(texture);
        cursor["rwTexture2"].setBinding(rwTexture);
        cursor["textureArray2"].setBinding(textureArray);
        cursor["rwTextureArray2"].setBinding(rwTextureArray);
        cursor["samplerState2"].setBinding(sampler);
        cursor["result"].setBinding(result);

        passEncoder->dispatchCompute(1, 1, 1);
        passEncoder->end();

        queue->submit(commandEncoder->finish());
        queue->waitOnHost();
    }

    compareComputeResult(device, result, std::array{1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 7.f, 8.f, 5.f});
}
