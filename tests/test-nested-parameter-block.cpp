#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

ComPtr<IBuffer> createBuffer(IDevice* device, uint32_t data, ResourceState defaultState)
{
    uint32_t initialData[] = {data, data, data, data};
    BufferDesc bufferDesc = {};
    bufferDesc.size = sizeof(initialData);
    bufferDesc.format = Format::Undefined;
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

GPU_TEST_CASE("nested-parameter-block", ALL)
{
    if (!device->hasFeature(Feature::ParameterBlock))
        SKIP("no support for parameter blocks");

    ComPtr<IShaderProgram> shaderProgram;
    slang::ProgramLayout* slangReflection = nullptr;
    REQUIRE_CALL(loadAndLinkProgram(
        device,
        "test-nested-parameter-block",
        "computeMain",
        shaderProgram.writeRef(),
        &slangReflection
    ));

    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

    std::vector<ComPtr<IBuffer>> buffers;

    for (uint32_t i = 0; i < 6; i++)
    {
        buffers.push_back(createBuffer(device, i, ResourceState::ShaderResource));
    }
    ComPtr<IBuffer> resultBuffer = createBuffer(device, 0, ResourceState::UnorderedAccess);

    ComPtr<IShaderObject> materialObject;
    {
        REQUIRE_CALL(device->createShaderObject(
            nullptr,
            slangReflection->findTypeByName("MaterialSystem"),
            ShaderObjectContainerType::None,
            materialObject.writeRef()
        ));
        ShaderCursor cursor(materialObject);
        cursor["cb"].setData(uint4{1000, 1000, 1000, 1000});
        cursor["data"].setBinding(buffers[2]);
        materialObject->finalize();
    }

    ComPtr<IShaderObject> sceneObject;
    {
        REQUIRE_CALL(device->createShaderObject(
            nullptr,
            slangReflection->findTypeByName("Scene"),
            ShaderObjectContainerType::None,
            sceneObject.writeRef()
        ));
        ShaderCursor cursor(sceneObject);
        cursor["sceneCb"].setData(uint4{100, 100, 100, 100});
        cursor["data"].setBinding(buffers[1]);
        cursor["material"].setObject(materialObject);
        sceneObject->finalize();
    }

    ComPtr<IShaderObject> cbObject;
    {
        REQUIRE_CALL(device->createShaderObject(
            nullptr,
            slangReflection->findTypeByName("PerView"),
            ShaderObjectContainerType::None,
            cbObject.writeRef()
        ));
        ShaderCursor cursor(cbObject);
        cursor["value"].setData(uint4{20, 20, 20, 20});
        cbObject->finalize();
    }

    ComPtr<IShaderObject> rootObject;
    REQUIRE_CALL(device->createRootShaderObject(shaderProgram, rootObject.writeRef()));
    ShaderCursor cursor(rootObject);
    cursor["resultBuffer"].setBinding(resultBuffer);
    cursor["scene"].setObject(sceneObject);
    cursor["perView"].setObject(cbObject);

    // We have done all the set up work, now it is time to start recording a command buffer for
    // GPU execution.
    {
        auto queue = device->getQueue(QueueType::Graphics);
        auto commandEncoder = queue->createCommandEncoder();

        auto passEncoder = commandEncoder->beginComputePass();
        passEncoder->bindPipeline(pipeline, rootObject);
        passEncoder->dispatchCompute(1, 1, 1);
        passEncoder->end();

        queue->submit(commandEncoder->finish());
        queue->waitOnHost();
    }

    compareComputeResult(device, resultBuffer, makeArray<uint32_t>(1123u, 1123u, 1123u, 1123u));
}

// In this test, we change the method of feeding data to a parameter block.
// We first create the root shader object, and feed data directly to the ParameterBlock instead of
// using `setObject`, because we want to cover more cases on Metal.
// On Metal, ParameterBlock variable will have the different type layout because we will map that
// object to ArgumentBuffer, so RHI has to explicity change the layout by applying Argument Buffer Tier2
// rule, otherwise the size of such variable will always be 0, and all the `setData` call could fail.
GPU_TEST_CASE("nested-parameter-block-2", ALL)
{
    if (!device->hasFeature("parameter-block"))
        SKIP("no support for parameter blocks");

    ComPtr<IShaderProgram> shaderProgram;
    slang::ProgramLayout* slangReflection = nullptr;
    REQUIRE_CALL(loadAndLinkProgram(
        device,
        "test-nested-parameter-block",
        "computeMain",
        shaderProgram.writeRef(),
        &slangReflection
    ));

    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

    std::vector<ComPtr<IBuffer>> buffers;

    for (uint32_t i = 0; i < 2; i++)
    {
        buffers.push_back(createBuffer(device, i + 1, ResourceState::ShaderResource));
    }
    ComPtr<IBuffer> resultBuffer = createBuffer(device, 0, ResourceState::UnorderedAccess);

    ComPtr<IShaderObject> rootObject;
    REQUIRE_CALL(device->createRootShaderObject(shaderProgram, rootObject.writeRef()));
    ShaderCursor cursor(rootObject);

    {
        cursor["scene"]["sceneCb"]["value"].setData(uint4{100, 100, 100, 100});

        cursor["scene"]["data"].setBinding(buffers[0]);

        cursor["scene"]["material"]["cb"]["value"].setData(uint4{1000, 1000, 1000, 1000});
        cursor["scene"]["material"]["data"].setBinding(buffers[1]);

        cursor["perView"]["value"].setData(uint4{20, 20, 20, 20});
    }

    cursor["resultBuffer"].setBinding(resultBuffer);
    rootObject->finalize();

    // We have done all the set up work, now it is time to start recording a command buffer for
    // GPU execution.
    {
        auto queue = device->getQueue(QueueType::Graphics);
        auto commandEncoder = queue->createCommandEncoder();

        auto passEncoder = commandEncoder->beginComputePass();
        passEncoder->bindPipeline(pipeline, rootObject);
        passEncoder->dispatchCompute(1, 1, 1);
        passEncoder->end();

        queue->submit(commandEncoder->finish());
        queue->waitOnHost();
    }

    compareComputeResult(device, resultBuffer, makeArray<uint32_t>(1123u, 1123u, 1123u, 1123u));
}
