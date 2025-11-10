#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

static ComPtr<IBuffer> createBuffer(IDevice* device, uint32_t content)
{
    ComPtr<IBuffer> buffer;
    BufferDesc bufferDesc = {};
    bufferDesc.size = sizeof(uint32_t);
    bufferDesc.format = Format::Undefined;
    bufferDesc.elementSize = sizeof(float);
    bufferDesc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess | BufferUsage::CopyDestination |
                       BufferUsage::CopySource;
    bufferDesc.defaultState = ResourceState::UnorderedAccess;
    bufferDesc.memoryType = MemoryType::DeviceLocal;

    ComPtr<IBuffer> numbersBuffer;
    REQUIRE_CALL(device->createBuffer(bufferDesc, (void*)&content, buffer.writeRef()));

    return buffer;
}

GPU_TEST_CASE("sampler-array", D3D12 | Vulkan | Metal)
{
    if (device->getDeviceType() == DeviceType::Vulkan && SLANG_APPLE_FAMILY)
        SKIP("not supported on MoltenVK");
    if (device->getDeviceType() == DeviceType::Metal)
        SKIP("skipped due to regression in Slang v2025.18.2");
    if (!device->hasFeature(Feature::ParameterBlock))
        SKIP("no support for parameter blocks");

    ComPtr<IShaderProgram> shaderProgram;
    slang::ProgramLayout* slangReflection = nullptr;
    REQUIRE_CALL(
        loadAndLinkProgram(device, "test-sampler-array", "computeMain", shaderProgram.writeRef(), &slangReflection)
    );

    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

    std::vector<ComPtr<ISampler>> samplers;
    ComPtr<ITexture> texture;
    ComPtr<IBuffer> buffer = createBuffer(device, 0);

    {
        TextureDesc textureDesc = {};
        textureDesc.type = TextureType::Texture2D;
        textureDesc.format = Format::RGBA8Unorm;
        textureDesc.size.width = 2;
        textureDesc.size.height = 2;
        textureDesc.size.depth = 1;
        textureDesc.mipCount = 2;
        textureDesc.memoryType = MemoryType::DeviceLocal;
        textureDesc.usage = TextureUsage::ShaderResource | TextureUsage::CopyDestination;
        textureDesc.defaultState = ResourceState::ShaderResource;
        uint32_t data[] = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF};
        SubresourceData subResourceData[2] = {{data, 8, 16}, {data, 8, 16}};
        REQUIRE_CALL(device->createTexture(textureDesc, subResourceData, texture.writeRef()));
    }

    for (uint32_t i = 0; i < 32; i++)
    {
        SamplerDesc desc = {};
        ComPtr<ISampler> sampler;
        REQUIRE_CALL(device->createSampler(desc, sampler.writeRef()));
        samplers.push_back(sampler);
    }

    ComPtr<IShaderObject> s1 =
        device->createShaderObject(slangReflection->findTypeByName("S1"), ShaderObjectContainerType::None);
    {
        auto cursor = ShaderCursor(s1);
        for (uint32_t i = 0; i < 32; i++)
        {
            cursor["samplers"][i].setBinding(samplers[i]);
            cursor["tex"][i].setBinding(texture);
        }
        cursor["data"].setData(1.0f);
    }
    s1->finalize();

    ComPtr<IShaderObject> g =
        device->createShaderObject(slangReflection->findTypeByName("S0"), ShaderObjectContainerType::None);
    {
        auto cursor = ShaderCursor(g);
        cursor["s"].setObject(s1);
        cursor["data"].setData(2.0f);
    }
    g->finalize();

    {
        auto queue = device->getQueue(QueueType::Graphics);
        auto commandEncoder = queue->createCommandEncoder();
        auto passEncoder = commandEncoder->beginComputePass();
        auto rootObject = passEncoder->bindPipeline(pipeline);
        auto cursor = ShaderCursor(rootObject);
        cursor["g"].setObject(g);
        cursor["buffer"].setBinding(buffer);
        passEncoder->dispatchCompute(1, 1, 1);
        passEncoder->end();

        queue->submit(commandEncoder->finish());
        queue->waitOnHost();
    }

    compareComputeResult(device, buffer, makeArray<float>(4.0f));
}
