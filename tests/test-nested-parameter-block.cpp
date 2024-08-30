#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

ComPtr<IBuffer> createBuffer(IDevice* device, uint32_t data, ResourceState defaultState)
{
    uint32_t initialData[] = {data, data, data, data};
    const int numberCount = SLANG_COUNT_OF(initialData);
    BufferDesc bufferDesc = {};
    bufferDesc.size = sizeof(initialData);
    bufferDesc.format = Format::Unknown;
    bufferDesc.elementSize = sizeof(uint32_t) * 4;
    bufferDesc.allowedStates = ResourceStateSet(
        ResourceState::ShaderResource,
        ResourceState::UnorderedAccess,
        ResourceState::CopyDestination,
        ResourceState::CopySource
    );
    bufferDesc.defaultState = defaultState;
    bufferDesc.memoryType = MemoryType::DeviceLocal;

    ComPtr<IBuffer> numbersBuffer;
    REQUIRE_CALL(device->createBuffer(bufferDesc, (void*)initialData, numbersBuffer.writeRef()));
    return numbersBuffer;
}

struct uint4
{
    uint32_t x, y, z, w;
};

void testNestedParameterBlock(GpuTestContext* ctx, DeviceType deviceType)
{
    ComPtr<IDevice> device = createTestingDevice(ctx, deviceType);

    ComPtr<ITransientResourceHeap> transientHeap;
    ITransientResourceHeap::Desc transientHeapDesc = {};
    transientHeapDesc.constantBufferSize = 4096;
    REQUIRE_CALL(device->createTransientResourceHeap(transientHeapDesc, transientHeap.writeRef()));

    ComPtr<IShaderProgram> shaderProgram;
    slang::ProgramLayout* slangReflection;
    REQUIRE_CALL(
        loadComputeProgram(device, shaderProgram, "test-nested-parameter-block", "computeMain", slangReflection)
    );

    ComputePipelineStateDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    ComPtr<IPipelineState> pipelineState;
    REQUIRE_CALL(device->createComputePipelineState(pipelineDesc, pipelineState.writeRef()));

    ComPtr<IShaderObject> shaderObject;
    REQUIRE_CALL(device->createMutableRootShaderObject(shaderProgram, shaderObject.writeRef()));

    std::vector<ComPtr<IBuffer>> srvBuffers;
    std::vector<ComPtr<IResourceView>> srvs;

    for (uint32_t i = 0; i < 6; i++)
    {
        srvBuffers.push_back(createBuffer(device, i, ResourceState::ShaderResource));
        IResourceView::Desc srvDesc = {};
        srvDesc.type = IResourceView::Type::ShaderResource;
        srvDesc.format = Format::Unknown;
        srvDesc.bufferRange.offset = 0;
        srvDesc.bufferRange.size = sizeof(uint32_t) * 4;
        srvs.push_back(device->createBufferView(srvBuffers[i], nullptr, srvDesc));
    }
    ComPtr<IBuffer> resultBuffer = createBuffer(device, 0, ResourceState::UnorderedAccess);
    IResourceView::Desc resultBufferViewDesc = {};
    resultBufferViewDesc.type = IResourceView::Type::UnorderedAccess;
    resultBufferViewDesc.format = Format::Unknown;
    resultBufferViewDesc.bufferRange.offset = 0;
    resultBufferViewDesc.bufferRange.size = sizeof(uint32_t) * 4;
    ComPtr<IResourceView> resultBufferView;
    REQUIRE_CALL(device->createBufferView(resultBuffer, nullptr, resultBufferViewDesc, resultBufferView.writeRef()));

    ComPtr<IShaderObject> materialObject;
    REQUIRE_CALL(device->createMutableShaderObject(
        slangReflection->findTypeByName("MaterialSystem"),
        ShaderObjectContainerType::None,
        materialObject.writeRef()
    ));

    ComPtr<IShaderObject> sceneObject;
    REQUIRE_CALL(device->createMutableShaderObject(
        slangReflection->findTypeByName("Scene"),
        ShaderObjectContainerType::None,
        sceneObject.writeRef()
    ));

    ShaderCursor cursor(shaderObject);
    cursor["resultBuffer"].setResource(resultBufferView);
    cursor["scene"].setObject(sceneObject);

    ComPtr<IShaderObject> globalCB;
    REQUIRE_CALL(device->createShaderObject(
        cursor[0].getTypeLayout()->getType(),
        ShaderObjectContainerType::None,
        globalCB.writeRef()
    ));

    cursor[0].setObject(globalCB);
    auto initialData = uint4{20, 20, 20, 20};
    globalCB->setData(ShaderOffset(), &initialData, sizeof(initialData));

    ShaderCursor sceneCursor(sceneObject);
    sceneCursor["sceneCb"].setData(uint4{100, 100, 100, 100});
    sceneCursor["data"].setResource(srvs[1]);
    sceneCursor["material"].setObject(materialObject);

    ShaderCursor materialCursor(materialObject);
    materialCursor["cb"].setData(uint4{1000, 1000, 1000, 1000});
    materialCursor["data"].setResource(srvs[2]);

    // We have done all the set up work, now it is time to start recording a command buffer for
    // GPU execution.
    {
        ICommandQueue::Desc queueDesc = {ICommandQueue::QueueType::Graphics};
        auto queue = device->createCommandQueue(queueDesc);

        auto commandBuffer = transientHeap->createCommandBuffer();
        auto encoder = commandBuffer->encodeComputeCommands();

        encoder->bindPipelineWithRootObject(pipelineState, shaderObject);

        encoder->dispatchCompute(1, 1, 1);
        encoder->endEncoding();
        commandBuffer->close();
        queue->executeCommandBuffer(commandBuffer);
        queue->waitOnHost();
    }

    compareComputeResult(device, resultBuffer, makeArray<uint32_t>(1123u, 1123u, 1123u, 1123u));
}

TEST_CASE("NestedParameterBlock")
{
    runGpuTests(testNestedParameterBlock, {DeviceType::D3D12, DeviceType::Vulkan});
}
