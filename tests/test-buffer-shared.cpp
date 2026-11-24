#include "testing.h"

#include <cstring>

using namespace rhi;
using namespace rhi::testing;

GPU_TEST_CASE("buffer-shared-host-visible", Vulkan)
{
    // Shared buffers in host-visible memory get their own exportable allocation. They must stay
    // mappable, take part in GPU copies and export a handle, like any other shared buffer.
    const uint32_t expected[] = {0x10203040, 0x50607080, 0x90A0B0C0, 0xD0E0F000};

    auto createSharedBuffer = [&](MemoryType memoryType, BufferUsage usage)
    {
        BufferDesc desc = {};
        desc.size = sizeof(expected);
        desc.usage = usage | BufferUsage::Shared;
        desc.memoryType = memoryType;
        ComPtr<IBuffer> buffer;
        REQUIRE_CALL(device->createBuffer(desc, nullptr, buffer.writeRef()));
        NativeHandle handle = {};
        REQUIRE_CALL(buffer->getSharedHandle(&handle));
        CHECK(handle);
        return buffer;
    };
    ComPtr<IBuffer> upload = createSharedBuffer(MemoryType::Upload, BufferUsage::CopySource);
    ComPtr<IBuffer> readback = createSharedBuffer(MemoryType::ReadBack, BufferUsage::CopyDestination);

    void* mapped = nullptr;
    REQUIRE_CALL(device->mapBuffer(upload, CpuAccessMode::Write, &mapped));
    std::memcpy(mapped, expected, sizeof(expected));
    REQUIRE_CALL(device->unmapBuffer(upload));

    auto queue = device->getQueue(QueueType::Graphics);
    auto encoder = queue->createCommandEncoder();
    encoder->copyBuffer(readback, 0, upload, 0, sizeof(expected));
    REQUIRE_CALL(queue->submit(encoder->finish()));
    REQUIRE_CALL(queue->waitOnHost());

    REQUIRE_CALL(device->mapBuffer(readback, CpuAccessMode::Read, &mapped));
    CHECK_EQ(std::memcmp(mapped, expected, sizeof(expected)), 0);
    REQUIRE_CALL(device->unmapBuffer(readback));
}

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
    // Reading back the buffer from srcDevice to make sure it's been filled in before reading anything back from
    // dstDevice
    // TODO: Implement actual synchronization (and not this hacky solution)
    compareComputeResult(srcDevice, srcBuffer, makeArray<float>(0.0f, 1.0f, 2.0f, 3.0f));

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
}
#endif
