#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

void testExistingDeviceHandle(GpuTestContext* ctx, DeviceType deviceType)
{
    ComPtr<IDevice> existingDevice = createTestingDevice(ctx, deviceType);
    DeviceNativeHandles handles;
    CHECK_CALL(existingDevice->getNativeDeviceHandles(&handles));

    ComPtr<IDevice> device;
    DeviceDesc deviceDesc = {};
    deviceDesc.deviceType = deviceType;
    deviceDesc.existingDeviceHandles.handles[0] = handles.handles[0];
    if (deviceType == DeviceType::Vulkan)
    {
        deviceDesc.existingDeviceHandles.handles[1] = handles.handles[1];
        deviceDesc.existingDeviceHandles.handles[2] = handles.handles[2];
    }
    deviceDesc.slang.slangGlobalSession = ctx->slangGlobalSession;
    auto searchPaths = getSlangSearchPaths();
    deviceDesc.slang.searchPaths = searchPaths.data();
    deviceDesc.slang.searchPathCount = searchPaths.size();
    CHECK_CALL(getRHI()->createDevice(deviceDesc, device.writeRef()));

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

        auto rootObject = device->createRootShaderObject(pipeline);
        ShaderCursor rootCursor(rootObject);
        // Bind buffer view to the root.
        rootCursor["buffer"].setBinding(buffer);
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

TEST_CASE("existing-device-handle")
{
    runGpuTests(
        testExistingDeviceHandle,
        {
            DeviceType::Vulkan,
            DeviceType::D3D12,
            DeviceType::CUDA,
        }
    );
}
