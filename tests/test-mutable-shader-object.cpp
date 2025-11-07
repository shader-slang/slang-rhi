#if 0
#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

GPU_TEST_CASE("mutable-shader-object", ALL)
{
    ComPtr<ITransientResourceHeap> transientHeap;
    ITransientResourceHeap::Desc transientHeapDesc = {};
    transientHeapDesc.constantBufferSize = 4096;
    REQUIRE_CALL(device->createTransientResourceHeap(transientHeapDesc, transientHeap.writeRef()));

    ComPtr<IShaderProgram> shaderProgram;
    slang::ProgramLayout* slangReflection = nullptr;
    REQUIRE_CALL(loadAndLinkProgram(device, "test-mutable-shader-object", "computeMain", shaderProgram, &slangReflection)
    );

    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    ComPtr<IPipeline> pipeline;
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

    {
        slang::TypeReflection* addTransformerType = slangReflection->findTypeByName("AddTransformer");

        ComPtr<IShaderObject> transformer;
        REQUIRE_CALL(device->createMutableShaderObject(
            addTransformerType,
            ShaderObjectContainerType::None,
            transformer.writeRef()
        ));
        // Set the `c` field of the `AddTransformer`.
        float c = 1.0f;
        ShaderCursor(transformer).getPath("c").setData(&c, sizeof(float));

        auto queue = device->getQueue(QueueType::Graphics);
        auto commandEncoder = queue->createCommandEncoder();

        auto rootObject = commandEncoder->preparePipeline(pipeline);

        auto entryPointCursor = ShaderCursor(rootObject->getEntryPoint(0));

        entryPointCursor.getPath("buffer").setBinding(buffer);

        // Bind the previously created transformer object to root object.
        ComPtr<IShaderObject> transformerVersion;
        transformer->getCurrentVersion(transientHeap, transformerVersion.writeRef());
        entryPointCursor.getPath("transformer").setObject(transformerVersion);

        ComputeState state;
        commandEncoder->prepareFinish(&state);
        commandEncoder->setComputeState(state);
        commandEncoder->dispatchCompute(1, 1, 1);

        rootObject = commandEncoder->preparePipeline(pipeline);
        entryPointCursor = ShaderCursor(rootObject->getEntryPoint(0));

        // Mutate `transformer` object and run again.
        c = 2.0f;
        ShaderCursor(transformer).getPath("c").setData(&c, sizeof(float));
        transformer->getCurrentVersion(transientHeap, transformerVersion.writeRef());
        entryPointCursor.getPath("buffer").setBinding(buffer);
        entryPointCursor.getPath("transformer").setObject(transformerVersion);

        commandEncoder->prepareFinish(&state);
        commandEncoder->setComputeState(state);
        commandEncoder->dispatchCompute(1, 1, 1);

        queue->submit(commandEncoder->finish());
        queue->waitOnHost();
    }

    compareComputeResult(device, buffer, makeArray<float>(3.0f, 4.0f, 5.0f, 6.0f));
}

#endif
