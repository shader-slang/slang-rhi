#include "testing.h"
#include <slang-rhi.h>
#include "core/smart-pointer.h"
#include "rhi-shared.h"
#include "device.h"

#include <barrier>
#include <thread>

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

GPU_TEST_CASE("device-lifetime-view-retains-texture-and-sampler", ALL | DontCreateDevice)
{
    for (bool validation : {false, true})
    {
        DeviceDesc deviceDesc = {};
        deviceDesc.deviceType = ctx->deviceType;
        deviceDesc.adapter = getSelectedDeviceAdapter(ctx->deviceType);
        deviceDesc.enableValidation = validation;
        // Initialize the backend singleton before measuring live objects.
        ComPtr<IDevice> warmup;
        REQUIRE_CALL(getRHI()->createDevice(deviceDesc, warmup.writeRef()));
        warmup.setNull();
#if SLANG_RHI_DEBUG
        const uint64_t objectsBefore = RefObject::getObjectCount();
#endif
        const uint64_t resourcesBefore = gResourceCount.load();
        {
            ComPtr<IDevice> testDevice;
            REQUIRE_CALL(getRHI()->createDevice(deviceDesc, testDevice.writeRef()));
            SamplerDesc samplerDesc = {};
            samplerDesc.minFilter = TextureFilteringMode::Point;
            ComPtr<ISampler> sampler = testDevice->createSampler(samplerDesc);
            // The CPU backend represents samplers with a null pointer.
            if (ctx->deviceType != DeviceType::CPU)
                REQUIRE(sampler);
            TextureDesc desc = {};
            desc.format = Format::RGBA8Unorm;
            desc.usage = TextureUsage::ShaderResource;
            desc.sampler = sampler;
            ComPtr<ITexture> texture = testDevice->createTexture(desc);
            REQUIRE(texture);
            ComPtr<ITextureView> view = texture->getDefaultView();
            REQUIRE(view);
            sampler.setNull();
            texture.setNull();
            testDevice.setNull();

            // The external view reference retains the texture externally. The texture
            // keeps its sampler alive internally and supplies the device pin.
            ComPtr<ITexture> recovered(view->getTexture());
            REQUIRE(recovered);
            CHECK_EQ(recovered->getDesc().format, Format::RGBA8Unorm);
            if (ctx->deviceType != DeviceType::CPU)
            {
                auto* retainedSampler = recovered->getDesc().sampler;
                REQUIRE(retainedSampler);
                CHECK_EQ(retainedSampler, desc.sampler);
                // A deferred sampler can remain allocated at zero references; verify ownership too.
                CHECK_GT(static_cast<Sampler*>(retainedSampler)->getInternalReferenceCount(), 0);
                CHECK_EQ(retainedSampler->getDesc().minFilter, samplerDesc.minFilter);
            }
            view.setNull();
            REQUIRE(recovered->getDefaultView());
        }
        CHECK_EQ(gResourceCount.load(), resourcesBefore);
#if SLANG_RHI_DEBUG
        CHECK_EQ(RefObject::getObjectCount(), objectsBefore);
#endif
    }
}

GPU_TEST_CASE("device-lifetime-default-view-cache", ALL | DontCreateDevice)
{
    auto warmup = createTestingDevice(ctx, ctx->deviceType, false);
    warmup.setNull();
    const uint64_t resourcesBefore = gResourceCount.load();
#if SLANG_RHI_DEBUG
    const uint64_t objectsBefore = RefObject::getObjectCount();
#endif
    {
        auto testDevice = createTestingDevice(ctx, ctx->deviceType, false);
        TextureDesc desc = {};
        desc.format = Format::RGBA8Unorm;
        desc.usage = TextureUsage::ShaderResource;
        auto texture = testDevice->createTexture(desc);
        REQUIRE(texture);
        ITextureView* identity = nullptr;
        {
            auto first = texture->getDefaultView();
            REQUIRE(first);
            identity = first.get();
            auto second = texture->getDefaultView();
            CHECK_EQ(second.get(), identity);
        }
        // The parent owns the allocation even when no consumer references exist.
        CHECK_EQ(identity->getTexture(), texture.get());
        auto again = texture->getDefaultView();
        CHECK_EQ(again.get(), identity);
        again.setNull();

        auto ordinary = texture->createView({});
        REQUIRE(ordinary);
        CHECK_NE(ordinary.get(), identity);
        {
            ComPtr<ITextureView> queried;
            REQUIRE(SLANG_SUCCEEDED(
                identity->queryInterface(ITextureView::getTypeGuid(), reinterpret_cast<void**>(queried.writeRef()))
            ));
            CHECK_EQ(queried.get(), identity);
        }
        texture.setNull();
        // An ordinary view protects the parent and, with it, the dormant default view.
        CHECK_EQ(ordinary->getTexture()->getDefaultView().get(), identity);
        auto survivor = ordinary->getTexture()->getDefaultView();
        ordinary.setNull();
        testDevice.setNull();
        CHECK_EQ(survivor->getTexture()->getDesc().format, Format::RGBA8Unorm);
        survivor.setNull();
    }
    CHECK_EQ(gResourceCount.load(), resourcesBefore);
#if SLANG_RHI_DEBUG
    CHECK_EQ(RefObject::getObjectCount(), objectsBefore);
#endif
}

GPU_TEST_CASE("device-lifetime-default-view-concurrent-publication", ALL | DontCreateDevice)
{
    auto warmup = createTestingDevice(ctx, ctx->deviceType, false);
    warmup.setNull();
    const uint64_t resourcesBefore = gResourceCount.load();
#if SLANG_RHI_DEBUG
    const uint64_t objectsBefore = RefObject::getObjectCount();
#endif
    {
        auto testDevice = createTestingDevice(ctx, ctx->deviceType, false);
        TextureDesc desc = {};
        desc.format = Format::RGBA8Unorm;
        desc.usage = TextureUsage::ShaderResource;
        // Each round races the first publication on a fresh texture.
        for (uint32_t round = 0; round < 8; ++round)
        {
            auto texture = testDevice->createTexture(desc);
            REQUIRE(texture);
            std::barrier start(8);
            std::atomic<ITextureView*> identity{nullptr};
            std::atomic<uint32_t> errors{0};
            std::vector<std::thread> threads;
            for (uint32_t i = 0; i < 8; ++i)
            {
                threads.emplace_back(
                    [&]
                    {
                        DeviceScope scope(testDevice);
                        start.arrive_and_wait();
                        auto view = texture->getDefaultView();
                        ITextureView* expected = nullptr;
                        if (!view)
                            ++errors;
                        else if (!identity.compare_exchange_strong(expected, view.get()) && expected != view.get())
                            ++errors;
                    }
                );
            }
            for (auto& thread : threads)
                thread.join();
            CHECK_EQ(errors.load(), 0);
            REQUIRE(identity.load());
            CHECK_EQ(texture->getDefaultView().get(), identity.load());
        }
    }
    CHECK_EQ(gResourceCount.load(), resourcesBefore);
#if SLANG_RHI_DEBUG
    CHECK_EQ(RefObject::getObjectCount(), objectsBefore);
#endif
}

GPU_TEST_CASE("device-lifetime-abandoned-recording", ALL | DontCreateDevice)
{
    auto warmup = createTestingDevice(ctx, ctx->deviceType, false);
    warmup.setNull();
    for (bool finish : {false, true})
    {
#if SLANG_RHI_DEBUG
        const uint64_t objectsBefore = RefObject::getObjectCount();
#endif
        const uint64_t resourcesBefore = gResourceCount.load();
        {
            auto testDevice = createTestingDevice(ctx, ctx->deviceType, false);
            auto queue = testDevice->getQueue(QueueType::Graphics);
            auto encoder = queue->createCommandEncoder();
            REQUIRE(encoder);
            BufferDesc desc = {};
            desc.size = 256;
            desc.usage = BufferUsage::CopyDestination;
            auto buffer = testDevice->createBuffer(desc);
            REQUIRE(buffer);
            const uint32_t data = 42;
            REQUIRE_CALL(encoder->uploadBufferData(buffer, 0, sizeof(data), &data));
            ComPtr<ICommandBuffer> commandBuffer;
            if (finish)
            {
                commandBuffer = encoder->finish();
                REQUIRE(commandBuffer);
                encoder.setNull();
            }
            buffer.setNull();
            queue.setNull();
            testDevice.setNull();
            // No submission: encoder/command-buffer destruction must free staging handles
            // before their device-owned heaps, including when returning a buffer to a pool.
        }
        CHECK_EQ(gResourceCount.load(), resourcesBefore);
#if SLANG_RHI_DEBUG
        CHECK_EQ(RefObject::getObjectCount(), objectsBefore);
#endif
    }
}
GPU_TEST_CASE("device-lifetime-retained-shader-entry-point", (ALL & ~CPU) | DontCreateDevice)
{
    auto warmup = createTestingDevice(ctx, ctx->deviceType, false);
    warmup.setNull();
#if SLANG_RHI_DEBUG
    const uint64_t objectsBefore = RefObject::getObjectCount();
#endif
    const uint64_t resourcesBefore = gResourceCount.load();
    {
        auto testDevice = createTestingDevice(ctx, ctx->deviceType, false);
        ComPtr<IShaderProgram> program;
        REQUIRE_CALL(
            loadProgram(testDevice, "test-shader-object-resource-tracking", "computeMain", program.writeRef())
        );
        auto root = testDevice->createRootShaderObject(program);
        REQUIRE(root);
        auto entryPoint = root->getEntryPoint(0);
        REQUIRE(entryPoint);
        BufferDesc desc = {};
        desc.size = sizeof(float);
        desc.usage = BufferUsage::ShaderResource;
        auto buffer = testDevice->createBuffer(desc);
        REQUIRE(buffer);
        REQUIRE_CALL(ShaderCursor(entryPoint)["buffer"].setBinding(buffer));
        buffer.setNull();
        root.setNull();
        program.setNull();
        testDevice.setNull();
        // A returned subobject is an independent external root, even after the parent
        // and application device/program handles are gone. Its dependencies remain usable.
        REQUIRE(ShaderCursor(entryPoint)["buffer"].isValid());
        REQUIRE_CALL(ShaderCursor(entryPoint)["buffer"].setBinding(Binding(static_cast<IBuffer*>(nullptr))));
    }
    CHECK_EQ(gResourceCount.load(), resourcesBefore);
#if SLANG_RHI_DEBUG
    CHECK_EQ(RefObject::getObjectCount(), objectsBefore);
#endif
}

GPU_TEST_CASE("device-lifetime-retained-shader-parameter-block", ALL | DontCreateDevice)
{
    auto warmup = createTestingDevice(ctx, ctx->deviceType, false);
    if (!warmup->hasFeature(Feature::ParameterBlock))
        SKIP("no support for parameter blocks");
    warmup.setNull();
#if SLANG_RHI_DEBUG
    const uint64_t objectsBefore = RefObject::getObjectCount();
#endif
    const uint64_t resourcesBefore = gResourceCount.load();
    {
        auto testDevice = createTestingDevice(ctx, ctx->deviceType, false);
        ComPtr<IShaderProgram> program;
        REQUIRE_CALL(loadProgram(testDevice, "test-nested-parameter-block", "computeMain", program.writeRef()));
        auto root = testDevice->createRootShaderObject(program);
        REQUIRE(root);

        // Retain the automatically created grandchild, exercising recursive program ownership.
        ComPtr<IShaderObject> material;
        {
            auto sceneCursor = ShaderCursor(root)["scene"];
            REQUIRE(sceneCursor.isValid());
            auto scene = root->getObject(sceneCursor.m_offset);
            REQUIRE(scene);
            auto materialCursor = ShaderCursor(scene)["material"];
            REQUIRE(materialCursor.isValid());
            REQUIRE_CALL(scene->getObject(materialCursor.m_offset, material.writeRef()));
            REQUIRE(material);
        }

        BufferDesc desc = {};
        desc.size = sizeof(uint32_t) * 4;
        desc.elementSize = desc.size;
        desc.usage = BufferUsage::ShaderResource;
        auto buffer = testDevice->createBuffer(desc);
        REQUIRE(buffer);
        REQUIRE_CALL(ShaderCursor(material)["data"].setBinding(buffer));
        IBuffer* borrowedBuffer = buffer.get(); // Kept alive by material's binding.
        buffer.setNull();
        root.setNull();
        program.setNull();
        testDevice.setNull();

        // Reflection, uniform storage, and resource bindings must survive all ancestor handles.
        auto valueCursor = ShaderCursor(material)["cb"]["value"];
        REQUIRE(valueCursor.isValid());
        const uint32_t values[] = {1, 2, 3, 4};
        REQUIRE_CALL(valueCursor.setData(values));
        REQUIRE(valueCursor.m_offset.uniformOffset + sizeof(values) <= material->getSize());
        const auto* data = static_cast<const uint8_t*>(material->getRawData());
        CHECK_EQ(::memcmp(data + valueCursor.m_offset.uniformOffset, values, sizeof(values)), 0);
        auto dataCursor = ShaderCursor(material)["data"];
        REQUIRE(dataCursor.isValid());
        REQUIRE_CALL(dataCursor.setBinding(borrowedBuffer));
    }
    CHECK_EQ(gResourceCount.load(), resourcesBefore);
#if SLANG_RHI_DEBUG
    CHECK_EQ(RefObject::getObjectCount(), objectsBefore);
#endif
}

// Submit real work on a fresh device, then drop every application reference while that work is
// still pending. The queue keeps the submitted command buffer alive until the GPU is done, and
// the command buffer keeps the resources it touched alive for the same reason, but both of
// those are references the RHI holds on its own behalf. If either were treated as an ordinary
// application reference it would keep the device alive, and since the device is what drains the
// queue on destruction, nothing would ever be released. Track multiple buffers to cover
// cleanup of every resource held by the command buffer.
static void checkNoLeakAfterReleasingInFlightWork(GpuTestContext* ctx, bool releaseDeviceFirst)
{
    const uint64_t resourceCountBefore = gResourceCount.load();

    {
        ComPtr<IDevice> device = createTestingDevice(ctx, ctx->deviceType, false);
        REQUIRE(device);

        std::vector<ComPtr<IBuffer>> buffers;
        ComPtr<ICommandQueue> queue = device->getQueue(QueueType::Graphics);
        ComPtr<ICommandEncoder> encoder = queue->createCommandEncoder();
        REQUIRE(encoder);

        for (uint32_t i = 0; i < 8; ++i)
        {
            BufferDesc bufferDesc = {};
            bufferDesc.size = 256;
            bufferDesc.memoryType = MemoryType::DeviceLocal;
            bufferDesc.usage = BufferUsage::CopyDestination;
            ComPtr<IBuffer> buffer;
            REQUIRE_CALL(device->createBuffer(bufferDesc, nullptr, buffer.writeRef()));
            const uint32_t data = 0x12345678 + i;
            REQUIRE_CALL(encoder->uploadBufferData(buffer, 0, sizeof(data), &data));
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
    SUBCASE("device-released-last")
    {
        checkNoLeakAfterReleasingInFlightWork(ctx, false);
    }
    SUBCASE("device-released-first")
    {
        checkNoLeakAfterReleasingInFlightWork(ctx, true);
    }
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
