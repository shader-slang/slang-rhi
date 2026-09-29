#include "testing.h"
#include <slang-rhi.h>
#include "core/smart-pointer.h"
#include "rhi-shared.h"
#include "device.h"

using namespace rhi;
using namespace rhi::testing;

GPU_TEST_CASE("device-lifetime", ALL | DontCreateDevice)
{
    // Create a device
    DeviceDesc deviceDesc = {};
    deviceDesc.deviceType = ctx->deviceType;
    deviceDesc.adapter = getSelectedDeviceAdapter(ctx->deviceType);
    ComPtr<IDevice> testDevice;
    REQUIRE_CALL(getRHI()->createDevice(deviceDesc, testDevice.writeRef()));

    Device* devicePtr = static_cast<Device*>(testDevice.get());

    // Create a buffer
    ComPtr<IBuffer> buffer;
    BufferDesc bufferDesc = {};
    bufferDesc.size = 1024;
    bufferDesc.usage = BufferUsage::ShaderResource;
    REQUIRE_CALL(testDevice->createBuffer(bufferDesc, nullptr, buffer.writeRef()));
    uint64_t deviceRefCountBuffer = devicePtr->getReferenceCount();

    // Create a texture
    ComPtr<ITexture> texture;
    TextureDesc textureDesc = {};
    textureDesc.format = Format::RGBA32Float;
    textureDesc.usage = TextureUsage::ShaderResource;
    REQUIRE_CALL(testDevice->createTexture(textureDesc, nullptr, texture.writeRef()));
    uint64_t deviceRefCountTexture = devicePtr->getReferenceCount();

    // Create a sampler
    ComPtr<ISampler> sampler;
    SamplerDesc samplerDesc = {};
    REQUIRE_CALL(testDevice->createSampler(samplerDesc, sampler.writeRef()));
    uint64_t deviceRefCountSampler = devicePtr->getReferenceCount();

    // Create acceleration structure
    ComPtr<IAccelerationStructure> accelerationStructure;
    if (testDevice->hasFeature(Feature::AccelerationStructure))
    {
        AccelerationStructureDesc accelerationStructureDesc = {};
        accelerationStructureDesc.size = 1024;
        REQUIRE_CALL(
            testDevice->createAccelerationStructure(accelerationStructureDesc, accelerationStructure.writeRef())
        );
    }
    uint64_t deviceRefCountAccelerationStructure = devicePtr->getReferenceCount();

    // Create fence
    ComPtr<IFence> fence;
    if (testDevice->getDeviceType() != DeviceType::D3D11)
    {
        FenceDesc fenceDesc = {};
        REQUIRE_CALL(testDevice->createFence(fenceDesc, fence.writeRef()));
    }
    uint64_t deviceRefCountFence = devicePtr->getReferenceCount();

    testDevice.setNull();

    CHECK(devicePtr->getReferenceCount() == deviceRefCountFence - 1);
    fence.setNull();

    CHECK(devicePtr->getReferenceCount() == deviceRefCountAccelerationStructure - 1);
    accelerationStructure.setNull();

    CHECK(devicePtr->getReferenceCount() == deviceRefCountSampler - 1);
    sampler.setNull();

    CHECK(devicePtr->getReferenceCount() == deviceRefCountTexture - 1);
    texture.setNull();

    CHECK(devicePtr->getReferenceCount() == deviceRefCountBuffer - 1);
    buffer.setNull();
}

// Submit real work on a fresh device, then drop every application reference while that work is
// still pending. The queue keeps the submitted command buffer alive until the GPU is done, and
// the command buffer keeps the resources it touched alive for the same reason, but both of
// those are references the RHI holds on its own behalf. If either were treated as an ordinary
// application reference it would keep the device alive, and since the device is what drains the
// queue on destruction, nothing would ever be released. `count` is how many buffers the command
// buffer ends up tracking, so the test also covers the multi-resource case.
static void checkNoLeakAfterReleasingInFlightWork(GpuTestContext* ctx, uint32_t count, bool releaseDeviceFirst)
{
    const uint64_t resourceCountBefore = gResourceCount.load();

    {
        ComPtr<IDevice> device = createTestingDevice(ctx, ctx->deviceType, false);
        REQUIRE(device);

        std::vector<ComPtr<IBuffer>> buffers;
        ComPtr<ICommandQueue> queue = device->getQueue(QueueType::Graphics);
        ComPtr<ICommandEncoder> encoder = queue->createCommandEncoder();
        REQUIRE(encoder);

        for (uint32_t i = 0; i < count; ++i)
        {
            BufferDesc bufferDesc = {};
            bufferDesc.size = 256;
            bufferDesc.memoryType = MemoryType::DeviceLocal;
            bufferDesc.usage = BufferUsage::CopyDestination;
            ComPtr<IBuffer> buffer;
            REQUIRE_CALL(device->createBuffer(bufferDesc, nullptr, buffer.writeRef()));
            const uint32_t data = 0x12345678 + i;
            encoder->uploadBufferData(buffer, 0, sizeof(data), &data);
            buffers.push_back(buffer);
        }

        ComPtr<ICommandBuffer> commandBuffer = encoder->finish();
        REQUIRE(commandBuffer);
        encoder.setNull();
        REQUIRE_CALL(queue->submit(commandBuffer));

        // Deliberately no `waitOnHost()` and no further submission: releasing the application
        // references has to be enough on its own to tear everything down.
        if (releaseDeviceFirst)
        {
            device.setNull();
            commandBuffer.setNull();
            buffers.clear();
            queue.setNull();
        }
        else
        {
            commandBuffer.setNull();
            buffers.clear();
            queue.setNull();
            device.setNull();
        }
    }

    CHECK_EQ(gResourceCount.load(), resourceCountBefore);
}

GPU_TEST_CASE("device-lifetime-in-flight-command-buffer", ALL | DontCreateDevice)
{
    checkNoLeakAfterReleasingInFlightWork(ctx, 1, false);
}

GPU_TEST_CASE("device-lifetime-in-flight-command-buffer-multiple-resources", ALL | DontCreateDevice)
{
    checkNoLeakAfterReleasingInFlightWork(ctx, 8, false);
}

GPU_TEST_CASE("device-lifetime-in-flight-command-buffer-device-released-first", ALL | DontCreateDevice)
{
    checkNoLeakAfterReleasingInFlightWork(ctx, 1, true);
}

// The same check for a dispatch that binds textures and samplers. A texture is bound through a
// texture view, so this covers the resources a command buffer reaches only indirectly.
GPU_TEST_CASE("device-lifetime-in-flight-command-buffer-bound-resources", (ALL & ~CPU) | DontCreateDevice)
{
    const uint64_t resourceCountBefore = gResourceCount.load();

    {
        ComPtr<IDevice> testDevice = createTestingDevice(ctx, ctx->deviceType, false);
        REQUIRE(testDevice);

        ComPtr<IShaderProgram> shaderProgram;
        REQUIRE_CALL(
            loadProgram(testDevice, "test-shader-object-resource-tracking", "computeMain", shaderProgram.writeRef())
        );
        ComputePipelineDesc pipelineDesc = {};
        pipelineDesc.program = shaderProgram.get();
        ComPtr<IComputePipeline> pipeline;
        REQUIRE_CALL(testDevice->createComputePipeline(pipelineDesc, pipeline.writeRef()));

        BufferDesc bufferDesc = {};
        bufferDesc.size = sizeof(float);
        bufferDesc.usage = BufferUsage::CopyDestination | BufferUsage::ShaderResource;
        const float bufferData[] = {10.f};
        ComPtr<IBuffer> buffer;
        REQUIRE_CALL(testDevice->createBuffer(bufferDesc, bufferData, buffer.writeRef()));

        TextureDesc textureDesc = {};
        textureDesc.size = {2, 2, 1};
        textureDesc.format = Format::R32Float;
        textureDesc.usage = TextureUsage::CopyDestination | TextureUsage::ShaderResource;
        const float textureData[] = {1.f, 2.f, 3.f, 4.f};
        SubresourceData subresourceData[] = {{textureData, sizeof(float) * 2, 0}};
        ComPtr<ITexture> texture;
        REQUIRE_CALL(testDevice->createTexture(textureDesc, subresourceData, texture.writeRef()));

        SamplerDesc samplerDesc = {};
        ComPtr<ISampler> sampler;
        REQUIRE_CALL(testDevice->createSampler(samplerDesc, sampler.writeRef()));

        BufferDesc resultBufferDesc = {};
        resultBufferDesc.size = 4 * sizeof(float);
        resultBufferDesc.usage = BufferUsage::CopySource | BufferUsage::UnorderedAccess;
        ComPtr<IBuffer> resultBuffer;
        REQUIRE_CALL(testDevice->createBuffer(resultBufferDesc, nullptr, resultBuffer.writeRef()));

        ComPtr<ICommandQueue> queue = testDevice->getQueue(QueueType::Graphics);
        ComPtr<ICommandEncoder> encoder = queue->createCommandEncoder();
        REQUIRE(encoder);
        auto passEncoder = encoder->beginComputePass();
        auto rootObject = passEncoder->bindPipeline(pipeline);
        ShaderCursor globalsCursor(rootObject);
        globalsCursor["globalBuffer"].setBinding(buffer);
        globalsCursor["globalTexture"].setBinding(texture);
        globalsCursor["globalSampler"].setBinding(sampler);
        ShaderCursor entryPointCursor(rootObject->getEntryPoint(0));
        entryPointCursor["buffer"].setBinding(buffer);
        entryPointCursor["texture"].setBinding(texture);
        entryPointCursor["sampler"].setBinding(sampler);
        entryPointCursor["resultBuffer"].setBinding(resultBuffer);
        passEncoder->dispatchCompute(1, 1, 1);
        passEncoder->end();

        ComPtr<ICommandBuffer> commandBuffer = encoder->finish();
        REQUIRE(commandBuffer);
        encoder.setNull();
        REQUIRE_CALL(queue->submit(commandBuffer));

        commandBuffer.setNull();
        buffer.setNull();
        texture.setNull();
        sampler.setNull();
        resultBuffer.setNull();
        pipeline.setNull();
        shaderProgram.setNull();
        queue.setNull();
        testDevice.setNull();
    }

    CHECK_EQ(gResourceCount.load(), resourceCountBefore);
}
