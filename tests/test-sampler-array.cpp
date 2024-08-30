#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

static ComPtr<IBuffer> createBuffer(IDevice* device, uint32_t content)
{
    ComPtr<IBuffer> buffer;
    IBuffer::Desc bufferDesc = {};
    bufferDesc.sizeInBytes = sizeof(uint32_t);
    bufferDesc.format = Format::Unknown;
    bufferDesc.elementSize = sizeof(float);
    bufferDesc.allowedStates = ResourceStateSet(
        ResourceState::ShaderResource,
        ResourceState::UnorderedAccess,
        ResourceState::CopyDestination,
        ResourceState::CopySource
    );
    bufferDesc.defaultState = ResourceState::UnorderedAccess;
    bufferDesc.memoryType = MemoryType::DeviceLocal;

    ComPtr<IBuffer> numbersBuffer;
    REQUIRE_CALL(device->createBuffer(bufferDesc, (void*)&content, buffer.writeRef()));

    return buffer;
}
void testSamplerArray(GpuTestContext* ctx, DeviceType deviceType)
{
    ComPtr<IDevice> device = createTestingDevice(ctx, deviceType);

    ComPtr<ITransientResourceHeap> transientHeap;
    ITransientResourceHeap::Desc transientHeapDesc = {};
    transientHeapDesc.constantBufferSize = 4096;
    REQUIRE_CALL(device->createTransientResourceHeap(transientHeapDesc, transientHeap.writeRef()));

    ComPtr<IShaderProgram> shaderProgram;
    slang::ProgramLayout* slangReflection;
    REQUIRE_CALL(loadComputeProgram(device, shaderProgram, "test-sampler-array", "computeMain", slangReflection));

    ComputePipelineStateDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    ComPtr<IPipelineState> pipelineState;
    REQUIRE_CALL(device->createComputePipelineState(pipelineDesc, pipelineState.writeRef()));

    std::vector<ComPtr<ISamplerState>> samplers;
    std::vector<ComPtr<IResourceView>> srvs;
    ComPtr<IResourceView> uav;
    ComPtr<ITexture> texture;
    ComPtr<IBuffer> buffer = createBuffer(device, 0);

    {
        IResourceView::Desc viewDesc = {};
        viewDesc.type = IResourceView::Type::UnorderedAccess;
        viewDesc.format = Format::Unknown;
        REQUIRE_CALL(device->createBufferView(buffer, nullptr, viewDesc, uav.writeRef()));
    }
    {
        ITexture::Desc textureDesc = {};
        textureDesc.type = IResource::Type::Texture2D;
        textureDesc.format = Format::R8G8B8A8_UNORM;
        textureDesc.size.width = 2;
        textureDesc.size.height = 2;
        textureDesc.size.depth = 1;
        textureDesc.numMipLevels = 2;
        textureDesc.memoryType = MemoryType::DeviceLocal;
        textureDesc.defaultState = ResourceState::ShaderResource;
        textureDesc.allowedStates.add(ResourceState::CopyDestination);
        uint32_t data[] = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF};
        ITexture::SubresourceData subResourceData[2] = {{data, 8, 16}, {data, 8, 16}};
        REQUIRE_CALL(device->createTexture(textureDesc, subResourceData, texture.writeRef()));
    }
    for (uint32_t i = 0; i < 32; i++)
    {
        ComPtr<IResourceView> srv;
        IResourceView::Desc viewDesc = {};
        viewDesc.type = IResourceView::Type::ShaderResource;
        viewDesc.format = Format::R8G8B8A8_UNORM;
        viewDesc.subresourceRange.layerCount = 1;
        viewDesc.subresourceRange.mipLevelCount = 1;
        REQUIRE_CALL(device->createTextureView(texture, viewDesc, srv.writeRef()));
        srvs.push_back(srv);
    }

    for (uint32_t i = 0; i < 32; i++)
    {
        ISamplerState::Desc desc = {};
        ComPtr<ISamplerState> sampler;
        REQUIRE_CALL(device->createSamplerState(desc, sampler.writeRef()));
        samplers.push_back(sampler);
    }

    ComPtr<IShaderObject> rootObject;
    device->createMutableRootShaderObject(shaderProgram, rootObject.writeRef());

    ComPtr<IShaderObject> g;
    device->createMutableShaderObject(
        slangReflection->findTypeByName("S0"),
        ShaderObjectContainerType::None,
        g.writeRef()
    );

    ComPtr<IShaderObject> s1;
    device->createMutableShaderObject(
        slangReflection->findTypeByName("S1"),
        ShaderObjectContainerType::None,
        s1.writeRef()
    );

    {
        auto cursor = ShaderCursor(s1);
        for (uint32_t i = 0; i < 32; i++)
        {
            cursor["samplers"][i].setSampler(samplers[i]);
            cursor["tex"][i].setResource(srvs[i]);
        }
        cursor["data"].setData(1.0f);
    }

    {
        auto cursor = ShaderCursor(g);
        cursor["s"].setObject(s1);
        cursor["data"].setData(2.0f);
    }

    {
        auto cursor = ShaderCursor(rootObject);
        cursor["g"].setObject(g);
        cursor["buffer"].setResource(uav);
    }

    {
        ICommandQueue::Desc queueDesc = {ICommandQueue::QueueType::Graphics};
        auto queue = device->createCommandQueue(queueDesc);

        auto commandBuffer = transientHeap->createCommandBuffer();
        {
            auto encoder = commandBuffer->encodeComputeCommands();
            encoder->bindPipelineWithRootObject(pipelineState, rootObject);
            encoder->dispatchCompute(1, 1, 1);
            encoder->endEncoding();
        }

        commandBuffer->close();
        queue->executeCommandBuffer(commandBuffer);
        queue->waitOnHost();
    }

    compareComputeResult(device, buffer, makeArray<float>(4.0f));
}

TEST_CASE("sampler-array")
{
    runGpuTests(testSamplerArray, {DeviceType::D3D12, DeviceType::Vulkan});
}
