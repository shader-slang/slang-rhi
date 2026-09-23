#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

#if SLANG_WIN64
GPU_TEST_CASE("buffer-shared-cuda", D3D12 | Vulkan | DontCreateDevice)
{
    if (!isDeviceTypeAvailable(DeviceType::CUDA))
        SKIP("CUDA not available");

    ComPtr<IDevice> srcDevice = createTestingDevice(ctx, ctx->deviceType);
    ComPtr<IDevice> dstDevice = createTestingDevice(ctx, DeviceType::CUDA);

    if (srcDevice->getInfo().adapterLUID != dstDevice->getInfo().adapterLUID)
        SKIP("Devices do not refer to the same physical device");

    // Create a shareable buffer using srcDevice, get its handle, then create a buffer using the handle using
    // dstDevice. Read back the buffer and check that its contents are correct.
    const int numberCount = 4;
    float initialData[] = {0.0f, 1.0f, 2.0f, 3.0f};
    BufferDesc bufferDesc = {};
    bufferDesc.size = numberCount * sizeof(float);
    bufferDesc.format = Format::Undefined;
    bufferDesc.elementSize = sizeof(float);
    bufferDesc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess | BufferUsage::CopyDestination |
                       BufferUsage::CopySource | BufferUsage::Shared;
    bufferDesc.defaultState = ResourceState::UnorderedAccess;
    bufferDesc.memoryType = MemoryType::DeviceLocal;

    ComPtr<IBuffer> srcBuffer;
    REQUIRE_CALL(srcDevice->createBuffer(bufferDesc, (void*)initialData, srcBuffer.writeRef()));

    NativeHandle sharedHandle;
    REQUIRE_CALL(srcBuffer->getSharedHandle(&sharedHandle));
    ComPtr<IBuffer> dstBuffer;
    REQUIRE_CALL(dstDevice->createBufferFromSharedHandle(sharedHandle, bufferDesc, dstBuffer.writeRef()));
    // Explicitly transfer ownership of the shared buffer from the producer (D3D12/Vulkan) to CUDA:
    // the producer hands it off to the CUDA queue, and CUDA takes it over before accessing it. On
    // Vulkan this performs the queue-family ownership transfer to/from VK_QUEUE_FAMILY_EXTERNAL; on
    // D3D12/CUDA the calls are no-ops. The producer's waitOnHost after the hand-off also ensures the
    // initial buffer fill (createBuffer initialData) has completed before CUDA reads - the guarantee
    // the removed compareComputeResult(srcDevice, ...) used to provide.
    auto srcQueue = srcDevice->getQueue(QueueType::Graphics);
    auto dstQueue = dstDevice->getQueue(QueueType::Graphics);
    {
        IResource* resources[] = {srcBuffer.get()};
        auto handOffEncoder = srcQueue->createCommandEncoder();
        REQUIRE_CALL(handOffEncoder->handOffShared(1, resources, dstQueue.get()));
        srcQueue->submit(handOffEncoder->finish());
        srcQueue->waitOnHost();
    }
    {
        IResource* resources[] = {dstBuffer.get()};
        auto takeOverEncoder = dstQueue->createCommandEncoder();
        REQUIRE_CALL(takeOverEncoder->takeOverShared(1, resources, srcQueue.get()));
        dstQueue->submit(takeOverEncoder->finish());
        dstQueue->waitOnHost();
    }

    const BufferDesc& testDesc = dstBuffer->getDesc();
    CHECK_EQ(testDesc.elementSize, sizeof(float));
    CHECK_EQ(testDesc.size, numberCount * sizeof(float));
    compareComputeResult(dstDevice, dstBuffer, makeArray<float>(0.0f, 1.0f, 2.0f, 3.0f));

    ComPtr<IShaderProgram> shaderProgram;
    REQUIRE_CALL(loadProgram(dstDevice, "test-compute-trivial", "computeMain", shaderProgram.writeRef()));

    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(dstDevice->createComputePipeline(pipelineDesc, pipeline.writeRef()));

    {
        auto queue = dstDevice->getQueue(QueueType::Graphics);
        auto commandEncoder = queue->createCommandEncoder();

        auto passEncoder = commandEncoder->beginComputePass();
        auto rootObject = passEncoder->bindPipeline(pipeline);
        ShaderCursor(rootObject)["buffer"].setBinding(dstBuffer);
        passEncoder->dispatchCompute(1, 1, 1);
        passEncoder->end();

        queue->submit(commandEncoder->finish());
        queue->waitOnHost();
    }

    compareComputeResult(dstDevice, dstBuffer, makeArray<float>(1.0f, 2.0f, 3.0f, 4.0f));

    // Round-trip ownership back to the producer: CUDA hands the buffer off to the producer's queue
    // (a no-op on CUDA), and the producer takes it over - on Vulkan this exercises the
    // EXTERNAL -> thisFamily queue-family acquire, the reverse of the initial hand-off. The producer
    // then reads back what CUDA wrote, confirming the data survived the round-trip.
    {
        IResource* resources[] = {dstBuffer.get()};
        auto handBackEncoder = dstQueue->createCommandEncoder();
        REQUIRE_CALL(handBackEncoder->handOffShared(1, resources, srcQueue.get()));
        dstQueue->submit(handBackEncoder->finish());
        dstQueue->waitOnHost();
    }
    {
        IResource* resources[] = {srcBuffer.get()};
        auto reclaimEncoder = srcQueue->createCommandEncoder();
        REQUIRE_CALL(reclaimEncoder->takeOverShared(1, resources, dstQueue.get()));
        srcQueue->submit(reclaimEncoder->finish());
        srcQueue->waitOnHost();
    }
    compareComputeResult(srcDevice, srcBuffer, makeArray<float>(1.0f, 2.0f, 3.0f, 4.0f));
}
#endif
