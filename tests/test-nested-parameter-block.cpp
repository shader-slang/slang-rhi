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
    bufferDesc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess | BufferUsage::CopyDestination |
                       BufferUsage::CopySource;
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

    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    ComPtr<IPipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

    ComPtr<IShaderObject> shaderObject;
    REQUIRE_CALL(device->createMutableRootShaderObject(shaderProgram, shaderObject.writeRef()));

    std::vector<ComPtr<IBuffer>> buffers;

    for (uint32_t i = 0; i < 6; i++)
    {
        buffers.push_back(createBuffer(device, i, ResourceState::ShaderResource));
    }
    ComPtr<IBuffer> resultBuffer = createBuffer(device, 0, ResourceState::UnorderedAccess);

    ComPtr<IShaderObject> materialObject;
    REQUIRE_CALL(device->createMutableShaderObject(
        slangReflection->findTypeByName("MaterialSystem"),
        ShaderObjectContainerType::None,
        materialObject.writeRef()
    ));

    ShaderCursor materialCursor(materialObject);
    materialCursor["cb"].setData(uint4{1000, 1000, 1000, 1000});
    materialCursor["data"].setBinding(buffers[2]);

    ComPtr<IShaderObject> sceneObject;
    REQUIRE_CALL(device->createMutableShaderObject(
        slangReflection->findTypeByName("Scene"),
        ShaderObjectContainerType::None,
        sceneObject.writeRef()
    ));

    ShaderCursor sceneCursor(sceneObject);
    sceneCursor["sceneCb"].setData(uint4{100, 100, 100, 100});
    sceneCursor["data"].setBinding(buffers[1]);
    sceneCursor["material"].setObject(materialObject);

    ShaderCursor cursor(shaderObject);
    cursor["resultBuffer"].setBinding(resultBuffer);
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

    // We have done all the set up work, now it is time to start recording a command buffer for
    // GPU execution.
    {
        ICommandQueue::Desc queueDesc = {ICommandQueue::QueueType::Graphics};
        auto queue = device->createCommandQueue(queueDesc);

        auto commandBuffer = transientHeap->createCommandBuffer();
        auto passEncoder = commandBuffer->beginComputePass();

        passEncoder->bindPipelineWithRootObject(pipeline, shaderObject);

        passEncoder->dispatchCompute(1, 1, 1);
        passEncoder->end();
        commandBuffer->close();
        queue->executeCommandBuffer(commandBuffer);
        queue->waitOnHost();
    }

    compareComputeResult(device, resultBuffer, makeArray<uint32_t>(1123u, 1123u, 1123u, 1123u));
}

TEST_CASE("nested-parameter-block")
{
    // Only supported on D3D12 and Vulkan.
    runGpuTests(
        testNestedParameterBlock,
        {
            DeviceType::D3D12,
            DeviceType::Vulkan,
        }
    );
}
