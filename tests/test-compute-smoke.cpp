#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

void testComputeSmoke(GpuTestContext* ctx, DeviceType deviceType)
{
    ComPtr<IDevice> device = createTestingDevice(ctx, deviceType);

    ComPtr<IShaderProgram> shaderProgram;
    slang::ProgramLayout* slangReflection;
    REQUIRE_CALL(loadComputeProgram(device, shaderProgram, "test-compute-smoke", "computeMain", slangReflection));

    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

    const int numberCount = 4;
    float initialData[] = {0.0f, 1.0f, 2.0f, 3.0f};
    BufferDesc bufferDesc = {};
    bufferDesc.size = numberCount * sizeof(float);
    bufferDesc.format = Format::Unknown;
    bufferDesc.elementSize = sizeof(float);
    bufferDesc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess | BufferUsage::CopyDestination |
                       BufferUsage::CopySource;
    bufferDesc.defaultState = ResourceState::UnorderedAccess;
    bufferDesc.memoryType = MemoryType::DeviceLocal;

    ComPtr<IBuffer> buffer;
    REQUIRE_CALL(device->createBuffer(bufferDesc, (void*)initialData, buffer.writeRef()));

    // We have done all the set up work, now it is time to start recording a command buffer for
    // GPU execution.
    {
        auto queue = device->getQueue(QueueType::Graphics);
        auto encoder = queue->createCommandEncoder();

        // Now we can use this type to create a shader object that can be bound to the root object.
        ComPtr<IShaderObject> transformer;
        REQUIRE_CALL(device->createShaderObject(
            nullptr,
            slangReflection->findTypeByName("AddTransformer"),
            ShaderObjectContainerType::None,
            transformer.writeRef()
        ));
        // Set the `c` field of the `AddTransformer`.
        float c = 1.0f;
        ShaderCursor(transformer)["c"].setData(&c, sizeof(float));
        transformer->finalize();

        auto rootObject = device->createRootShaderObject(pipeline);
        ShaderCursor cursor(rootObject->getEntryPoint(0)); // get a cursor the the first entry-point.
        // Bind buffer view to the entry point.
        cursor["buffer"].setBinding(buffer);
        // Bind the previously created transformer object to root object.
        cursor["transformer"].setObject(transformer);
        rootObject->finalize();

        auto passEncoder = encoder->beginComputePass();
        ComputeState state;
        state.pipeline = pipeline;
        state.rootObject = rootObject;
        passEncoder->setComputeState(state);
        passEncoder->dispatchCompute(1, 1, 1);
        passEncoder->end();

        queue->submit(encoder->finish());
        queue->waitOnHost();
    }

    compareComputeResult(device, buffer, makeArray<float>(11.0f, 12.0f, 13.0f, 14.0f));
}

TEST_CASE("compute-smoke")
{
    runGpuTests(
        testComputeSmoke,
        {
            DeviceType::D3D11,
            DeviceType::D3D12,
            DeviceType::Vulkan,
            DeviceType::Metal,
            DeviceType::CUDA,
            DeviceType::CPU,
            // DeviceType::WGPU,
        }
    );
}
