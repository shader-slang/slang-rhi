#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

void testComputeTrivial(GpuTestContext* ctx, DeviceType deviceType)
{
    ComPtr<IDevice> device = createTestingDevice(ctx, deviceType);

    ComPtr<IShaderProgram> shaderProgram;
    slang::ProgramLayout* slangReflection;
    REQUIRE_CALL(loadComputeProgram(device, shaderProgram, "test-compute-trivial", "computeMain", slangReflection));

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

        // Bind buffer view to the entry point.
        auto rootObject = device->createRootShaderObject(pipeline);
        ShaderCursor(rootObject)["buffer"].setBinding(buffer);
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

    compareComputeResult(device, buffer, makeArray<float>(1.0f, 2.0f, 3.0f, 4.0f));
}

TEST_CASE("compute-trivial")
{
    runGpuTests(
        testComputeTrivial,
        {
            DeviceType::D3D11,
            DeviceType::D3D12,
            DeviceType::Vulkan,
            DeviceType::Metal,
            DeviceType::CUDA,
            DeviceType::CPU,
            DeviceType::WGPU,
        }
    );
}
