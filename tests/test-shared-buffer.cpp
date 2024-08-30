#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

template<DeviceType DstDeviceType>
void testSharedBuffer(GpuTestContext* ctx, DeviceType deviceType)
{
    ComPtr<IDevice> srcDevice = createTestingDevice(ctx, deviceType);
    ComPtr<IDevice> dstDevice = createTestingDevice(ctx, DstDeviceType);

    // Create a shareable buffer using srcDevice, get its handle, then create a buffer using the handle using
    // dstDevice. Read back the buffer and check that its contents are correct.
    const int numberCount = 4;
    float initialData[] = {0.0f, 1.0f, 2.0f, 3.0f};
    IBufferResource::Desc bufferDesc = {};
    bufferDesc.sizeInBytes = numberCount * sizeof(float);
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
    bufferDesc.isShared = true;

    ComPtr<IBufferResource> srcBuffer;
    REQUIRE_CALL(srcDevice->createBufferResource(bufferDesc, (void*)initialData, srcBuffer.writeRef()));

    InteropHandle sharedHandle;
    REQUIRE_CALL(srcBuffer->getSharedHandle(&sharedHandle));
    ComPtr<IBufferResource> dstBuffer;
    REQUIRE_CALL(dstDevice->createBufferFromSharedHandle(sharedHandle, bufferDesc, dstBuffer.writeRef()));
    // Reading back the buffer from srcDevice to make sure it's been filled in before reading anything back from
    // dstDevice
    // TODO: Implement actual synchronization (and not this hacky solution)
    compareComputeResult(srcDevice, srcBuffer, makeArray<float>(0.0f, 1.0f, 2.0f, 3.0f));

    InteropHandle testHandle;
    REQUIRE_CALL(dstBuffer->getNativeResourceHandle(&testHandle));
    IBufferResource::Desc* testDesc = dstBuffer->getDesc();
    CHECK_EQ(testDesc->elementSize, sizeof(float));
    CHECK_EQ(testDesc->sizeInBytes, numberCount * sizeof(float));
    compareComputeResult(dstDevice, dstBuffer, makeArray<float>(0.0f, 1.0f, 2.0f, 3.0f));

    // Check that dstBuffer can be successfully used in a compute dispatch using dstDevice.
    ComPtr<ITransientResourceHeap> transientHeap;
    ITransientResourceHeap::Desc transientHeapDesc = {};
    transientHeapDesc.constantBufferSize = 4096;
    REQUIRE_CALL(dstDevice->createTransientResourceHeap(transientHeapDesc, transientHeap.writeRef()));

    ComPtr<IShaderProgram> shaderProgram;
    slang::ProgramLayout* slangReflection;
    REQUIRE_CALL(loadComputeProgram(dstDevice, shaderProgram, "test-compute-trivial", "computeMain", slangReflection));

    ComputePipelineStateDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    ComPtr<IPipelineState> pipelineState;
    REQUIRE_CALL(dstDevice->createComputePipelineState(pipelineDesc, pipelineState.writeRef()));

    ComPtr<IResourceView> bufferView;
    IResourceView::Desc viewDesc = {};
    viewDesc.type = IResourceView::Type::UnorderedAccess;
    viewDesc.format = Format::Unknown;
    REQUIRE_CALL(dstDevice->createBufferView(dstBuffer, nullptr, viewDesc, bufferView.writeRef()));

    {
        ICommandQueue::Desc queueDesc = {ICommandQueue::QueueType::Graphics};
        auto queue = dstDevice->createCommandQueue(queueDesc);

        auto commandBuffer = transientHeap->createCommandBuffer();
        auto encoder = commandBuffer->encodeComputeCommands();

        auto rootObject = encoder->bindPipeline(pipelineState);

        ShaderCursor rootCursor(rootObject);
        // Bind buffer view to the entry point.
        rootCursor.getPath("buffer").setResource(bufferView);

        encoder->dispatchCompute(1, 1, 1);
        encoder->endEncoding();
        commandBuffer->close();
        queue->executeCommandBuffer(commandBuffer);
        queue->waitOnHost();
    }

    compareComputeResult(dstDevice, dstBuffer, makeArray<float>(1.0f, 2.0f, 3.0f, 4.0f));
}

#if SLANG_WIN64
TEST_CASE("shared-buffer-cuda")
{
    runGpuTests(testSharedBuffer<DeviceType::CUDA>, {DeviceType::D3D12, DeviceType::Vulkan});
}
#endif
