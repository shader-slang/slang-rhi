#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

void testExistingDeviceHandle(GpuTestContext* ctx, DeviceType deviceType)
{
    ComPtr<IDevice> existingDevice = createTestingDevice(ctx, deviceType);
    IDevice::NativeHandles handles;
    CHECK_CALL(existingDevice->getNativeDeviceHandles(&handles));

    ComPtr<IDevice> device;
    IDevice::Desc deviceDesc = {};
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
    CHECK_CALL(rhiCreateDevice(&deviceDesc, device.writeRef()));

    ComPtr<ITransientResourceHeap> transientHeap;
    ITransientResourceHeap::Desc transientHeapDesc = {};
    transientHeapDesc.constantBufferSize = 4096;
    REQUIRE_CALL(device->createTransientResourceHeap(transientHeapDesc, transientHeap.writeRef()));

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
    bufferDesc.allowedStates = ResourceStateSet(
        ResourceState::ShaderResource,
        ResourceState::UnorderedAccess,
        ResourceState::CopyDestination,
        ResourceState::CopySource
    );
    bufferDesc.defaultState = ResourceState::UnorderedAccess;
    bufferDesc.memoryType = MemoryType::DeviceLocal;

    ComPtr<IBuffer> numbersBuffer;
    REQUIRE_CALL(device->createBuffer(bufferDesc, (void*)initialData, numbersBuffer.writeRef()));

    ComPtr<IResourceView> bufferView;
    IResourceView::Desc viewDesc = {};
    viewDesc.type = IResourceView::Type::UnorderedAccess;
    viewDesc.format = Format::Unknown;
    REQUIRE_CALL(device->createBufferView(numbersBuffer, nullptr, viewDesc, bufferView.writeRef()));

    // We have done all the set up work, now it is time to start recording a command buffer for
    // GPU execution.
    {
        ICommandQueue::Desc queueDesc = {ICommandQueue::QueueType::Graphics};
        auto queue = device->createCommandQueue(queueDesc);

        auto commandBuffer = transientHeap->createCommandBuffer();
        auto encoder = commandBuffer->encodeComputeCommands();

        auto rootObject = encoder->bindPipeline(pipeline);

        ShaderCursor rootCursor(rootObject);
        // Bind buffer view to the root.
        rootCursor.getPath("buffer").setResource(bufferView);

        encoder->dispatchCompute(1, 1, 1);
        encoder->endEncoding();
        commandBuffer->close();
        queue->executeCommandBuffer(commandBuffer);
        queue->waitOnHost();
    }

    compareComputeResult(device, numbersBuffer, makeArray<float>(1.0f, 2.0f, 3.0f, 4.0f));
}

TEST_CASE("existing-device-handle")
{
    runGpuTests(testExistingDeviceHandle, {DeviceType::Vulkan, DeviceType::D3D12, DeviceType::CUDA});
}
