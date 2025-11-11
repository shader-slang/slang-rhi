#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

GPU_TEST_CASE("buffer-from-handle", D3D12 | Vulkan)
{
    ComPtr<IShaderProgram> shaderProgram;
    REQUIRE_CALL(loadProgram(device, "test-compute-trivial", "computeMain", shaderProgram.writeRef()));

    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

    const int numberCount = 4;
    float initialData[] = {0.0f, 1.0f, 2.0f, 3.0f};
    BufferDesc bufferDesc = {};
    bufferDesc.size = numberCount * sizeof(float);
    bufferDesc.format = Format::Undefined;
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
        auto commandEncoder = queue->createCommandEncoder();

        auto passEncoder = commandEncoder->beginComputePass();
        auto rootObject = passEncoder->bindPipeline(pipeline);
        ShaderCursor(rootObject)["buffer"].setBinding(buffer);
        passEncoder->dispatchCompute(1, 1, 1);
        passEncoder->end();

        queue->submit(commandEncoder->finish());
        queue->waitOnHost();
    }

    compareComputeResult(device, buffer, makeArray<float>(1.0f, 2.0f, 3.0f, 4.0f));
}
