#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

void testMutableShaderObject(GpuTestContext* ctx, DeviceType deviceType)
{
    ComPtr<IDevice> device = createTestingDevice(ctx, deviceType);

    ComPtr<ITransientResourceHeap> transientHeap;
    ITransientResourceHeap::Desc transientHeapDesc = {};
    transientHeapDesc.constantBufferSize = 4096;
    REQUIRE_CALL(device->createTransientResourceHeap(transientHeapDesc, transientHeap.writeRef()));

    ComPtr<IShaderProgram> shaderProgram;
    slang::ProgramLayout* slangReflection;
    REQUIRE_CALL(loadComputeProgram(device, shaderProgram, "test-mutable-shader-object", "computeMain", slangReflection)
    );

    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    ComPtr<IPipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

    float initialData[] = {0.0f, 1.0f, 2.0f, 3.0f};
    const int numberCount = SLANG_COUNT_OF(initialData);
    BufferDesc bufferDesc = {};
    bufferDesc.size = sizeof(initialData);
    bufferDesc.format = Format::Unknown;
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

        ICommandQueue::Desc queueDesc = {ICommandQueue::QueueType::Graphics};
        auto queue = device->createCommandQueue(queueDesc);

        auto commandBuffer = transientHeap->createCommandBuffer();
        auto encoder = commandBuffer->encodeComputeCommands();

        auto rootObject = encoder->bindPipeline(pipeline);

        auto entryPointCursor = ShaderCursor(rootObject->getEntryPoint(0));

        entryPointCursor.getPath("buffer").setBinding(buffer);

        // Bind the previously created transformer object to root object.
        ComPtr<IShaderObject> transformerVersion;
        transformer->getCurrentVersion(transientHeap, transformerVersion.writeRef());
        entryPointCursor.getPath("transformer").setObject(transformerVersion);

        encoder->dispatchCompute(1, 1, 1);
        encoder->endEncoding();

        auto barrierEncoder = commandBuffer->encodeResourceCommands();
        barrierEncoder
            ->bufferBarrier(1, buffer.readRef(), ResourceState::UnorderedAccess, ResourceState::UnorderedAccess);
        barrierEncoder->endEncoding();

        encoder = commandBuffer->encodeComputeCommands();

        rootObject = encoder->bindPipeline(pipeline);
        entryPointCursor = ShaderCursor(rootObject->getEntryPoint(0));

        // Mutate `transformer` object and run again.
        c = 2.0f;
        ShaderCursor(transformer).getPath("c").setData(&c, sizeof(float));
        transformer->getCurrentVersion(transientHeap, transformerVersion.writeRef());
        entryPointCursor.getPath("buffer").setBinding(buffer);
        entryPointCursor.getPath("transformer").setObject(transformerVersion);
        encoder->dispatchCompute(1, 1, 1);
        encoder->endEncoding();

        commandBuffer->close();
        queue->executeCommandBuffer(commandBuffer);
        queue->waitOnHost();
    }

    compareComputeResult(device, buffer, makeArray<float>(3.0f, 4.0f, 5.0f, 6.0f));
}

TEST_CASE("mutable-shader-object")
{
    runGpuTests(
        testMutableShaderObject,
        {
            DeviceType::D3D11,
            DeviceType::D3D12,
            DeviceType::Vulkan,
            // DeviceType::Metal,
            DeviceType::CUDA,
            DeviceType::CPU,
        }
    );
}
