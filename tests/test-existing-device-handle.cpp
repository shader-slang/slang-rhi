#include "testing.h"

using namespace gfx;
using namespace gfx::testing;

void testExistingDeviceHandle(GpuTestContext* ctx, DeviceType deviceType)
{
    ComPtr<IDevice> existingDevice = createTestingDevice(ctx, deviceType);
    IDevice::InteropHandles handles;
    GFX_CHECK_CALL(existingDevice->getNativeDeviceHandles(&handles));

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
    GFX_CHECK_CALL(gfxCreateDevice(&deviceDesc, device.writeRef()));

    ComPtr<ITransientResourceHeap> transientHeap;
    ITransientResourceHeap::Desc transientHeapDesc = {};
    transientHeapDesc.constantBufferSize = 4096;
    GFX_CHECK_CALL_ABORT(
        device->createTransientResourceHeap(transientHeapDesc, transientHeap.writeRef()));

    ComPtr<IShaderProgram> shaderProgram;
    slang::ProgramLayout* slangReflection;
    GFX_CHECK_CALL_ABORT(loadComputeProgram(device, shaderProgram, "test-compute-trivial", "computeMain", slangReflection));

    ComputePipelineStateDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    ComPtr<gfx::IPipelineState> pipelineState;
    GFX_CHECK_CALL_ABORT(
        device->createComputePipelineState(pipelineDesc, pipelineState.writeRef()));

    const int numberCount = 4;
    float initialData[] = { 0.0f, 1.0f, 2.0f, 3.0f };
    IBufferResource::Desc bufferDesc = {};
    bufferDesc.sizeInBytes = numberCount * sizeof(float);
    bufferDesc.format = gfx::Format::Unknown;
    bufferDesc.elementSize = sizeof(float);
    bufferDesc.allowedStates = ResourceStateSet(
        ResourceState::ShaderResource,
        ResourceState::UnorderedAccess,
        ResourceState::CopyDestination,
        ResourceState::CopySource);
    bufferDesc.defaultState = ResourceState::UnorderedAccess;
    bufferDesc.memoryType = MemoryType::DeviceLocal;

    ComPtr<IBufferResource> numbersBuffer;
    GFX_CHECK_CALL_ABORT(device->createBufferResource(
        bufferDesc,
        (void*)initialData,
        numbersBuffer.writeRef()));

    ComPtr<IResourceView> bufferView;
    IResourceView::Desc viewDesc = {};
    viewDesc.type = IResourceView::Type::UnorderedAccess;
    viewDesc.format = Format::Unknown;
    GFX_CHECK_CALL_ABORT(
        device->createBufferView(numbersBuffer, nullptr, viewDesc, bufferView.writeRef()));

    // We have done all the set up work, now it is time to start recording a command buffer for
    // GPU execution.
    {
        ICommandQueue::Desc queueDesc = { ICommandQueue::QueueType::Graphics };
        auto queue = device->createCommandQueue(queueDesc);

        auto commandBuffer = transientHeap->createCommandBuffer();
        auto encoder = commandBuffer->encodeComputeCommands();

        auto rootObject = encoder->bindPipeline(pipelineState);

        ShaderCursor rootCursor(rootObject);
        // Bind buffer view to the root.
        rootCursor.getPath("buffer").setResource(bufferView);

        encoder->dispatchCompute(1, 1, 1);
        encoder->endEncoding();
        commandBuffer->close();
        queue->executeCommandBuffer(commandBuffer);
        queue->waitOnHost();
    }

    compareComputeResult(
        device,
        numbersBuffer,
        makeArray<float>(1.0f, 2.0f, 3.0f, 4.0f));
}

TEST_CASE("existing-device-handle")
{
    runGpuTests(testExistingDeviceHandle, {DeviceType::Vulkan, DeviceType::D3D12, DeviceType::CUDA});
}
