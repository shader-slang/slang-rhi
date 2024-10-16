#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

void testRootMutableShaderObject(GpuTestContext* ctx, DeviceType deviceType)
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

    ComPtr<IShaderObject> rootObject;
    device->createMutableRootShaderObject(shaderProgram, rootObject.writeRef());
    auto entryPointCursor = ShaderCursor(rootObject->getEntryPoint(0));
    entryPointCursor.getPath("buffer").setBinding(buffer);

    slang::TypeReflection* addTransformerType = slangReflection->findTypeByName("AddTransformer");
    ComPtr<IShaderObject> transformer;
    REQUIRE_CALL(
        device->createMutableShaderObject(addTransformerType, ShaderObjectContainerType::None, transformer.writeRef())
    );
    entryPointCursor.getPath("transformer").setObject(transformer);

    // Set the `c` field of the `AddTransformer`.
    float c = 1.0f;
    ShaderCursor(transformer).getPath("c").setData(&c, sizeof(float));

    {
        auto queue = device->getQueue(QueueType::Graphics);

        auto commandBuffer = transientHeap->createCommandBuffer();
        {
            auto passEncoder = commandBuffer->beginComputePass();
            passEncoder->bindPipelineWithRootObject(pipeline, rootObject);
            passEncoder->dispatchCompute(1, 1, 1);
            passEncoder->end();
        }

        // Mutate `transformer` object and run again.
        c = 2.0f;
        ShaderCursor(transformer).getPath("c").setData(&c, sizeof(float));
        {
            auto passEncoder = commandBuffer->beginComputePass();
            passEncoder->bindPipelineWithRootObject(pipeline, rootObject);
            passEncoder->dispatchCompute(1, 1, 1);
            passEncoder->end();
        }

        commandBuffer->close();
        queue->submit(commandBuffer);
        queue->waitOnHost();
    }

    compareComputeResult(device, buffer, makeArray<float>(3.0f, 4.0f, 5.0f, 6.0f));
}

TEST_CASE("root-mutable-shader-object")
{
    runGpuTests(
        testRootMutableShaderObject,
        {
            DeviceType::D3D12,
            // DeviceType::Vulkan,
        }
    );
}
