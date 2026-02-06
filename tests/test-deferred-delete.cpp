#include "testing.h"

#include "../src/device.h"

using namespace rhi;
using namespace rhi::testing;

// This test verifies that the deferred delete mechanism keeps GPU resources alive until the next submit.
GPU_TEST_CASE("deferred-delete", ALL)
{
    // Determine which resource types are deferred delete for the given device type.
    // This is implementation defined, but we need to know for testing purposes.
    bool deferredBuffer = false;
    bool deferredTexture = false;
    bool deferredSampler = false;
    bool deferredAccel = false;

    switch (device->getDeviceType())
    {
    case ::DeviceType::D3D11:
        // D3D11 already handles deferred release internally.
        break;
    case ::DeviceType::D3D12:
        // D3D12 deferred deletes all resources.
        deferredBuffer = true;
        deferredTexture = true;
        deferredSampler = true;
        deferredAccel = true;
        break;
    case ::DeviceType::Vulkan:
        // Vulkan deferred deletes all resources.
        deferredBuffer = true;
        deferredTexture = true;
        deferredSampler = true;
        deferredAccel = true;
        break;
    case ::DeviceType::Metal:
        // Metal deferred deletes all resources.
        deferredBuffer = true;
        deferredTexture = true;
        deferredSampler = true;
        deferredAccel = true;
        break;
    case ::DeviceType::CPU:
        // CPU devices dispatch commands synchronously, so resources can be deleted immediately.
        break;
    case ::DeviceType::CUDA:
        // CUDA deferred deletes all resources except samplers which have no GPU representation.
        deferredBuffer = true;
        deferredTexture = true;
        deferredAccel = true;
        break;
    case ::DeviceType::WGPU:
        // WGPU already handles deferred release internally.
        break;
    default:
        break;
    }

    auto queue = device->getQueue(QueueType::Graphics);
    uint64_t countBegin = gResourceCount;
    uint64_t countBefore = gResourceCount;

    // Create and release a buffer.
    {
        BufferDesc bufferDesc = {};
        bufferDesc.size = 256;
        bufferDesc.memoryType = MemoryType::DeviceLocal;
        bufferDesc.usage = BufferUsage::ShaderResource;

        ComPtr<IBuffer> buffer;
        REQUIRE_CALL(device->createBuffer(bufferDesc, nullptr, buffer.writeRef()));
        buffer = nullptr;
    }

    // Check buffer is still alive due to deferred delete.
    if (deferredBuffer)
    {
        CHECK_GT(gResourceCount, countBefore);
    }
    countBefore = gResourceCount;

    // Create and release a texture.
    {
        TextureDesc textureDesc = {};
        textureDesc.type = TextureType::Texture2D;
        textureDesc.format = Format::RGBA8Unorm;
        textureDesc.size = {16, 16, 1};
        textureDesc.memoryType = MemoryType::DeviceLocal;
        textureDesc.usage = TextureUsage::ShaderResource;

        ComPtr<ITexture> texture;
        REQUIRE_CALL(device->createTexture(textureDesc, nullptr, texture.writeRef()));
        texture = nullptr;
    }

    // Check texture is still alive due to deferred delete.
    if (deferredTexture)
    {
        CHECK_GT(gResourceCount, countBefore);
    }
    countBefore = gResourceCount;

    // Create and release a sampler.
    {
        SamplerDesc samplerDesc = {};

        ComPtr<ISampler> sampler;
        REQUIRE_CALL(device->createSampler(samplerDesc, sampler.writeRef()));
        sampler = nullptr;
    }

    // Check sampler is still alive due to deferred delete.
    if (deferredSampler)
    {
        CHECK_GT(gResourceCount, countBefore);
    }
    countBefore = gResourceCount;

    // Create and release an acceleration structure (if supported).
    if (device->hasFeature(Feature::AccelerationStructure))
    {
        AccelerationStructureDesc accelDesc = {};
        accelDesc.size = 1024;

        ComPtr<IAccelerationStructure> accel;
        REQUIRE_CALL(device->createAccelerationStructure(accelDesc, accel.writeRef()));
        accel = nullptr;
    }

    // Check acceleration structure is still alive due to deferred delete (if supported).
    if (deferredAccel && device->hasFeature(Feature::AccelerationStructure))
    {
        CHECK_GT(gResourceCount, countBefore);
    }
    countBefore = gResourceCount;

    // Do a submit - this should trigger executeDeferredDeletes.
    {
        auto encoder = queue->createCommandEncoder();
        queue->submit(encoder->finish());
    }

    // CUDA backend doesn't always trigger deferred deletes on submit for now.
    // Force by waiting on host to ensure all GPU work is done.
    if (device->getDeviceType() == DeviceType::CUDA)
    {
        queue->waitOnHost();
    }

    // All deferred resources should now be deleted.
    CHECK_LE(gResourceCount.load(), countBegin);

    // Wait for GPU work to complete.
    queue->waitOnHost();
}


// Stress test that verifies deferred delete works correctly with actual GPU work.
// This creates temporary buffers, uses them in compute shaders, and releases them.
// If deferred delete isn't working, the GPU would read from deleted buffers and produce wrong results.
GPU_TEST_CASE("deferred-delete-stress", ALL)
{
    ComPtr<IShaderProgram> writeValueProgram;
    REQUIRE_CALL(loadProgram(device, "test-deferred-delete", "writeValue", writeValueProgram.writeRef()));

    ComputePipelineDesc writeValuePipelineDesc = {};
    writeValuePipelineDesc.program = writeValueProgram.get();
    ComPtr<IComputePipeline> writeValuePipeline;
    REQUIRE_CALL(device->createComputePipeline(writeValuePipelineDesc, writeValuePipeline.writeRef()));

    ComPtr<IShaderProgram> accumulateProgram;
    REQUIRE_CALL(loadProgram(device, "test-deferred-delete", "accumulate", accumulateProgram.writeRef()));

    ComputePipelineDesc accumulatePipelineDesc = {};
    accumulatePipelineDesc.program = accumulateProgram.get();
    ComPtr<IComputePipeline> accumulatePipeline;
    REQUIRE_CALL(device->createComputePipeline(accumulatePipelineDesc, accumulatePipeline.writeRef()));

    auto queue = device->getQueue(QueueType::Graphics);

    static const size_t kEntryCount = 1024 * 1024;

    // Create accumulation buffer initialized to 0.
    BufferDesc accumBufferDesc = {};
    accumBufferDesc.size = kEntryCount * sizeof(uint32_t);
    accumBufferDesc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess | BufferUsage::CopySource;
    accumBufferDesc.memoryType = MemoryType::DeviceLocal;

    std::vector<uint32_t> zeroData(kEntryCount, 0);
    ComPtr<IBuffer> accumBuffer;
    REQUIRE_CALL(device->createBuffer(accumBufferDesc, zeroData.data(), accumBuffer.writeRef()));

    // Buffer desc for temporary buffers.
    BufferDesc tempBufferDesc = {};
    tempBufferDesc.size = kEntryCount * sizeof(uint32_t);
    tempBufferDesc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess;
    tempBufferDesc.memoryType = MemoryType::DeviceLocal;

    const uint32_t iterations = 100;

    for (uint32_t i = 0; i < iterations; ++i)
    {
        // Create a temporary buffer.
        ComPtr<IBuffer> tempBuffer;
        REQUIRE_CALL(device->createBuffer(tempBufferDesc, nullptr, tempBuffer.writeRef()));

        // Submit 1: Write iteration counter to temp buffer.
        {
            auto encoder = queue->createCommandEncoder();
            auto pass = encoder->beginComputePass();
            auto rootObject = pass->bindPipeline(writeValuePipeline);
            ShaderCursor cursor(rootObject);
            cursor["buffer"].setBinding(tempBuffer);
            cursor["value"].setData(i);
            pass->dispatchCompute(kEntryCount / 256, 1, 1);
            pass->end();
            queue->submit(encoder->finish());
        }

        // Submit 2: Add temp buffer value to accumulation buffer.
        {
            auto encoder = queue->createCommandEncoder();
            auto pass = encoder->beginComputePass();
            auto rootObject = pass->bindPipeline(accumulatePipeline);
            ShaderCursor cursor(rootObject);
            cursor["accumBuffer"].setBinding(accumBuffer);
            cursor["srcBuffer"].setBinding(tempBuffer);
            pass->dispatchCompute(kEntryCount / 256, 1, 1);
            pass->end();
            queue->submit(encoder->finish());
        }

        // Release the temp buffer - it should be deferred since GPU may still be using it.
        tempBuffer = nullptr;
    }

    queue->waitOnHost();

    std::vector<uint32_t> resultData(kEntryCount);
    device->readBuffer(accumBuffer, 0, kEntryCount * sizeof(uint32_t), resultData.data());
    // Expected result: sum of 0 + 1 + 2 + ... + (iterations-1) = iterations * (iterations - 1) / 2
    uint32_t expected = iterations * (iterations - 1) / 2;
    for (size_t i = 0; i < kEntryCount; ++i)
        CHECK_EQ(resultData[i], expected);
}
