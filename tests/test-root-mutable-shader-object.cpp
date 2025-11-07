#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

// TODO fix for other backends
GPU_TEST_CASE("root-mutable-shader-object", WGPU)
{
    ComPtr<IShaderProgram> shaderProgram;
    slang::ProgramLayout* slangReflection = nullptr;
    REQUIRE_CALL(
        loadAndLinkProgram(device, "test-mutable-shader-object", "computeMain", shaderProgram, &slangReflection)
    );

    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

    float initialData[] = {0.0f, 1.0f, 2.0f, 3.0f};
    const int numberCount = SLANG_COUNT_OF(initialData);
    BufferDesc bufferDesc = {};
    bufferDesc.size = sizeof(initialData);
    bufferDesc.format = Format::Undefined;
    bufferDesc.elementSize = sizeof(float);
    bufferDesc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess | BufferUsage::CopyDestination |
                       BufferUsage::CopySource;
    bufferDesc.defaultState = ResourceState::UnorderedAccess;
    bufferDesc.memoryType = MemoryType::DeviceLocal;

    ComPtr<IBuffer> buffer;
    REQUIRE_CALL(device->createBuffer(bufferDesc, (void*)initialData, buffer.writeRef()));

    ComPtr<IShaderObject> rootObject;
    device->createRootShaderObject(shaderProgram, rootObject.writeRef());
    auto entryPointCursor = ShaderCursor(rootObject->getEntryPoint(0));
    entryPointCursor["buffer"].setBinding(buffer);

    slang::TypeReflection* addTransformerType = slangReflection->findTypeByName("AddTransformer");
    ComPtr<IShaderObject> transformer;
    REQUIRE_CALL(
        device->createShaderObject(addTransformerType, ShaderObjectContainerType::None, transformer.writeRef())
    );
    entryPointCursor["transformer"].setObject(transformer);

    // Set the `c` field of the `AddTransformer`.
    float c = 1.0f;
    ShaderCursor(transformer)["c"].setData(&c, sizeof(float));

    {
        auto queue = device->getQueue(QueueType::Graphics);
        auto commandEncoder = queue->createCommandEncoder();
        auto passEncoder = commandEncoder->beginComputePass();
        passEncoder->bindPipeline(pipeline, rootObject);
        passEncoder->dispatchCompute(1, 1, 1);

        // Mutate `transformer` object and run again.
        c = 2.0f;
        ShaderCursor(transformer)["c"].setData(&c, sizeof(float));

        passEncoder->dispatchCompute(1, 1, 1);
        passEncoder->end();

        queue->submit(commandEncoder->finish());
        queue->waitOnHost();
    }

    compareComputeResult(device, buffer, makeArray<float>(3.0f, 4.0f, 5.0f, 6.0f));
}
