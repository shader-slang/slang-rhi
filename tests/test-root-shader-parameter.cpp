#include "testing.h"

using namespace rhi;
using namespace testing;

static ComPtr<IBufferResource> createBuffer(IDevice* device, uint32_t content)
{
    ComPtr<IBufferResource> buffer;
    IBufferResource::Desc bufferDesc = {};
    bufferDesc.sizeInBytes = sizeof(uint32_t);
    bufferDesc.format = Format::Unknown;
    bufferDesc.elementSize = sizeof(float);
    bufferDesc.allowedStates = ResourceStateSet(
        ResourceState::ShaderResource,
        ResourceState::UnorderedAccess,
        ResourceState::CopyDestination,
        ResourceState::CopySource);
    bufferDesc.defaultState = ResourceState::UnorderedAccess;
    bufferDesc.memoryType = MemoryType::DeviceLocal;

    ComPtr<IBufferResource> numbersBuffer;
    REQUIRE_CALL(
        device->createBufferResource(bufferDesc, (void*)&content, buffer.writeRef()));

    return buffer;
}
void testRootShaderParameter(GpuTestContext* ctx, DeviceType deviceType)
{
    ComPtr<IDevice> device = createTestingDevice(ctx, deviceType);

    ComPtr<ITransientResourceHeap> transientHeap;
    ITransientResourceHeap::Desc transientHeapDesc = {};
    transientHeapDesc.constantBufferSize = 4096;
    REQUIRE_CALL(
        device->createTransientResourceHeap(transientHeapDesc, transientHeap.writeRef()));

    ComPtr<IShaderProgram> shaderProgram;
    slang::ProgramLayout* slangReflection;
    REQUIRE_CALL(loadComputeProgram(device, shaderProgram, "test-root-shader-parameter", "computeMain", slangReflection));

    ComputePipelineStateDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    ComPtr<IPipelineState> pipelineState;
    REQUIRE_CALL(
        device->createComputePipelineState(pipelineDesc, pipelineState.writeRef()));

    std::vector<ComPtr<IBufferResource>> buffers;
    std::vector<ComPtr<IResourceView>> srvs, uavs;

    for (uint32_t i = 0; i < 9; i++)
    {
        buffers.push_back(createBuffer(device, i == 0 ? 10 : i));

        ComPtr<IResourceView> bufferView;
        IResourceView::Desc viewDesc = {};
        viewDesc.type = IResourceView::Type::UnorderedAccess;
        viewDesc.format = Format::Unknown;
        REQUIRE_CALL(
            device->createBufferView(buffers[i], nullptr, viewDesc, bufferView.writeRef()));
        uavs.push_back(bufferView);

        viewDesc.type = IResourceView::Type::ShaderResource;
        viewDesc.format = Format::Unknown;
        REQUIRE_CALL(
            device->createBufferView(buffers[i], nullptr, viewDesc, bufferView.writeRef()));
        srvs.push_back(bufferView);
    }

    ComPtr<IShaderObject> rootObject;
    device->createMutableRootShaderObject(shaderProgram, rootObject.writeRef());

    ComPtr<IShaderObject> g, s1, s2;
    device->createMutableShaderObject(
        slangReflection->findTypeByName("S0"), ShaderObjectContainerType::None, g.writeRef());
    device->createMutableShaderObject(
        slangReflection->findTypeByName("S1"), ShaderObjectContainerType::None, s1.writeRef());
    device->createMutableShaderObject(
        slangReflection->findTypeByName("S1"), ShaderObjectContainerType::None, s2.writeRef());

    {
        auto cursor = ShaderCursor(s1);
        cursor["c0"].setResource(srvs[2]);
        cursor["c1"].setResource(uavs[3]);
        cursor["c2"].setResource(srvs[4]);
    }
    {
        auto cursor = ShaderCursor(s2);
        cursor["c0"].setResource(srvs[5]);
        cursor["c1"].setResource(uavs[6]);
        cursor["c2"].setResource(srvs[7]);
    }
    {
        auto cursor = ShaderCursor(g);
        cursor["b0"].setResource(srvs[0]);
        cursor["b1"].setResource(srvs[1]);
        cursor["s1"].setObject(s1);
        cursor["s2"].setObject(s2);
    }
    {
        auto cursor = ShaderCursor(rootObject);
        cursor["g"].setObject(g);
        cursor["buffer"].setResource(uavs[8]);
    }

    {
        ICommandQueue::Desc queueDesc = { ICommandQueue::QueueType::Graphics };
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

    compareComputeResult(
        device, buffers[8], makeArray<uint32_t>(10 - 1 + 2 - 3 + 4 + 5 - 6 + 7));
}

TEST_CASE("root-shader-parameter")
{
    runGpuTests(testRootShaderParameter, {DeviceType::D3D12, DeviceType::Vulkan});
}
