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
    BufferDesc bufferDesc = {};
    bufferDesc.size = numberCount * sizeof(float);
    bufferDesc.format = Format::Unknown;
    bufferDesc.elementSize = sizeof(float);
    bufferDesc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess | BufferUsage::CopyDestination |
                       BufferUsage::CopySource;
    bufferDesc.defaultState = ResourceState::UnorderedAccess;
    bufferDesc.memoryType = MemoryType::DeviceLocal;
    bufferDesc.isShared = true;

    ComPtr<IBuffer> srcBuffer;
    REQUIRE_CALL(srcDevice->createBuffer(bufferDesc, (void*)initialData, srcBuffer.writeRef()));

    NativeHandle sharedHandle;
    REQUIRE_CALL(srcBuffer->getSharedHandle(&sharedHandle));
    ComPtr<IBuffer> dstBuffer;
    REQUIRE_CALL(dstDevice->createBufferFromSharedHandle(sharedHandle, bufferDesc, dstBuffer.writeRef()));
    // Reading back the buffer from srcDevice to make sure it's been filled in before reading anything back from
    // dstDevice
    // TODO: Implement actual synchronization (and not this hacky solution)
    compareComputeResult(srcDevice, srcBuffer, makeArray<float>(0.0f, 1.0f, 2.0f, 3.0f));

    const BufferDesc& testDesc = dstBuffer->getDesc();
    CHECK_EQ(testDesc.elementSize, sizeof(float));
    CHECK_EQ(testDesc.size, numberCount * sizeof(float));
    compareComputeResult(dstDevice, dstBuffer, makeArray<float>(0.0f, 1.0f, 2.0f, 3.0f));

    // Check that dstBuffer can be successfully used in a compute dispatch using dstDevice.
    ComPtr<ITransientResourceHeap> transientHeap;
    ITransientResourceHeap::Desc transientHeapDesc = {};
    transientHeapDesc.constantBufferSize = 4096;
    REQUIRE_CALL(dstDevice->createTransientResourceHeap(transientHeapDesc, transientHeap.writeRef()));

    ComPtr<IShaderProgram> shaderProgram;
    slang::ProgramLayout* slangReflection;
    REQUIRE_CALL(loadComputeProgram(dstDevice, shaderProgram, "test-compute-trivial", "computeMain", slangReflection));

    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    ComPtr<IPipeline> pipeline;
    REQUIRE_CALL(dstDevice->createComputePipeline(pipelineDesc, pipeline.writeRef()));

    {
        ICommandQueue::Desc queueDesc = {ICommandQueue::QueueType::Graphics};
        auto queue = dstDevice->createCommandQueue(queueDesc);

        auto commandBuffer = transientHeap->createCommandBuffer();
        auto passEncoder = commandBuffer->beginComputePass();

        auto rootObject = passEncoder->bindPipeline(pipeline);

        ShaderCursor rootCursor(rootObject);
        // Bind buffer view to the entry point.
        rootCursor.getPath("buffer").setBinding(dstBuffer);

        passEncoder->dispatchCompute(1, 1, 1);
        passEncoder->end();
        commandBuffer->close();
        queue->executeCommandBuffer(commandBuffer);
        queue->waitOnHost();
    }

    compareComputeResult(dstDevice, dstBuffer, makeArray<float>(1.0f, 2.0f, 3.0f, 4.0f));
}

#if SLANG_WIN64
TEST_CASE("shared-buffer-cuda")
{
    if (!rhiIsDeviceTypeSupported(DeviceType::CUDA))
        SKIP("CUDA not supported");

    runGpuTests(
        testSharedBuffer<DeviceType::CUDA>,
        {
            DeviceType::D3D12,
            DeviceType::Vulkan,
        }
    );
}
#endif
