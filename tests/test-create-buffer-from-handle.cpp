#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

void testCreateBufferFromHandle(GpuTestContext* ctx, DeviceType deviceType)
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

    ComPtr<IBuffer> originalNumbersBuffer;
    REQUIRE_CALL(device->createBuffer(bufferDesc, (void*)initialData, originalNumbersBuffer.writeRef()));

    NativeHandle handle;
    originalNumbersBuffer->getNativeHandle(&handle);
    ComPtr<IBuffer> buffer;
    REQUIRE_CALL(device->createBufferFromNativeHandle(handle, bufferDesc, buffer.writeRef()));
    compareComputeResult(device, buffer, makeArray<float>(0.0f, 1.0f, 2.0f, 3.0f));

    // We have done all the set up work, now it is time to start recording a command buffer for
    // GPU execution.
    {
        auto queue = device->getQueue(QueueType::Graphics);
        auto encoder = queue->createCommandEncoder();

        auto rootObject = device->createRootShaderObject(pipeline);
        ShaderCursor rootCursor(rootObject);
        // Bind buffer view to the entry point.
        rootCursor["buffer"].setBinding(buffer);
        rootObject->finalize();

        encoder->beginComputePass();
        ComputeState state;
        state.pipeline = pipeline;
        state.rootObject = rootObject;
        encoder->setComputeState(state);
        encoder->dispatchCompute(1, 1, 1);
        encoder->endComputePass();

        queue->submit(encoder->finish());
        queue->waitOnHost();
    }

    compareComputeResult(device, buffer, makeArray<float>(1.0f, 2.0f, 3.0f, 4.0f));
}

TEST_CASE("create-buffer-from-handle")
{
    runGpuTests(
        testCreateBufferFromHandle,
        {
            DeviceType::D3D12,
            DeviceType::Vulkan,
        }
    );
}
