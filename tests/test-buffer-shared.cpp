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

    // Hand the initialized buffer off to the external (CUDA) consumer: waiting on the producer's
    // queue releases it to VK_QUEUE_FAMILY_EXTERNAL, after which CUDA may import and access it.
    srcDevice->getQueue(QueueType::Graphics)->waitOnHost();

    NativeHandle sharedHandle;
    REQUIRE_CALL(srcBuffer->getSharedHandle(&sharedHandle));
    ComPtr<IBuffer> dstBuffer;
    REQUIRE_CALL(dstDevice->createBufferFromSharedHandle(sharedHandle, bufferDesc, dstBuffer.writeRef()));

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

    // Ping-pong back to the producer: reading srcBuffer on srcDevice reclaims ownership from the
    // external queue family, and the producer must observe CUDA's writes. The host waits on both
    // queues (CUDA's waitOnHost above and readBuffer's own wait) order the hand-off.
    compareComputeResult(srcDevice, srcBuffer, makeArray<float>(1.0f, 2.0f, 3.0f, 4.0f));
}
#endif
