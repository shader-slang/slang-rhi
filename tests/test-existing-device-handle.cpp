#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

GPU_TEST_CASE("existing-device-handle", D3D12 | Vulkan | CUDA)
{
    DeviceNativeHandles handles;
    CHECK_CALL(device->getNativeDeviceHandles(&handles));

    ComPtr<IDevice> newDevice;
    DeviceDesc newDeviceDesc = {};
    newDeviceDesc.deviceType = device->getDeviceInfo().deviceType;
    newDeviceDesc.existingDeviceHandles.handles[0] = handles.handles[0];
    if (newDeviceDesc.deviceType == DeviceType::Vulkan)
    {
        newDeviceDesc.existingDeviceHandles.handles[1] = handles.handles[1];
        newDeviceDesc.existingDeviceHandles.handles[2] = handles.handles[2];
    }
    newDeviceDesc.slang.slangGlobalSession = device->getSlangSession()->getGlobalSession();
    auto searchPaths = getSlangSearchPaths();
    newDeviceDesc.slang.searchPaths = searchPaths.data();
    newDeviceDesc.slang.searchPathCount = searchPaths.size();
    CHECK_CALL(getRHI()->createDevice(newDeviceDesc, newDevice.writeRef()));

    ComPtr<IShaderProgram> shaderProgram;
    slang::ProgramLayout* slangReflection;
    REQUIRE_CALL(loadComputeProgram(newDevice, shaderProgram, "test-compute-trivial", "computeMain", slangReflection));

    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(newDevice->createComputePipeline(pipelineDesc, pipeline.writeRef()));

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
    REQUIRE_CALL(newDevice->createBuffer(bufferDesc, (void*)initialData, buffer.writeRef()));

    // We have done all the set up work, now it is time to start recording a command buffer for
    // GPU execution.
    {
        auto queue = newDevice->getQueue(QueueType::Graphics);
        auto commandEncoder = queue->createCommandEncoder();

        auto passEncoder = commandEncoder->beginComputePass();
        auto rootObject = passEncoder->bindPipeline(pipeline);
        ShaderCursor(rootObject)["buffer"].setBinding(buffer);
        passEncoder->dispatchCompute(1, 1, 1);
        passEncoder->end();

        queue->submit(commandEncoder->finish());
        queue->waitOnHost();
    }

    compareComputeResult(newDevice, buffer, makeArray<float>(1.0f, 2.0f, 3.0f, 4.0f));
}
