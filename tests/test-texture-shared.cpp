#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

#if SLANG_WIN64
static void setUpAndRunShader(
    IDevice* device,
    ComPtr<ITexture> tex,
    ComPtr<IBuffer> buffer,
    const char* entryPoint,
    ComPtr<ISampler> sampler = nullptr
)
{
    ComPtr<IShaderProgram> shaderProgram;
    REQUIRE_CALL(loadProgram(device, "trivial-copy", entryPoint, shaderProgram.writeRef()));

    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

    // We have done all the set up work, now it is time to start recording a command buffer for
    // GPU execution.
    {
        auto queue = device->getQueue(QueueType::Graphics);
        auto commandEncoder = queue->createCommandEncoder();

        auto passEncoder = commandEncoder->beginComputePass();
        auto rootObject = passEncoder->bindPipeline(pipeline);
        ShaderCursor entryPointCursor(rootObject->getEntryPoint(0)); // get a cursor the the first entry-point.
        entryPointCursor["width"].setData(tex->getDesc().size.width);
        entryPointCursor["height"].setData(tex->getDesc().size.height);
        // Bind texture view to the entry point
        entryPointCursor["tex"].setBinding(tex);
        if (sampler)
            entryPointCursor["sampler"].setBinding(sampler);
        // Bind buffer view to the entry point.
        entryPointCursor["buffer"].setBinding(buffer);
        passEncoder->dispatchCompute(1, 1, 1);
        passEncoder->end();

        queue->submit(commandEncoder->finish());
        queue->waitOnHost();
    }
}

static ComPtr<ITexture> createTexture(IDevice* device, Extent3D extent, Format format, SubresourceData* initialData)
{
    TextureDesc texDesc = {};
    texDesc.type = TextureType::Texture2D;
    texDesc.mipCount = 1;
    texDesc.size = extent;
    texDesc.usage = TextureUsage::ShaderResource | TextureUsage::UnorderedAccess | TextureUsage::CopyDestination |
                    TextureUsage::CopySource | TextureUsage::Shared;
    texDesc.defaultState = ResourceState::UnorderedAccess;
    texDesc.format = format;

    ComPtr<ITexture> inTex;
    REQUIRE_CALL(device->createTexture(texDesc, initialData, inTex.writeRef()));
    return inTex;
}

template<typename T>
ComPtr<IBuffer> createBuffer(IDevice* device, int size, void* initialData)
{
    BufferDesc bufferDesc = {};
    bufferDesc.size = size * sizeof(T);
    bufferDesc.format = Format::Undefined;
    bufferDesc.elementSize = sizeof(T);
    bufferDesc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess | BufferUsage::CopyDestination |
                       BufferUsage::CopySource;
    bufferDesc.defaultState = ResourceState::UnorderedAccess;
    bufferDesc.memoryType = MemoryType::DeviceLocal;

    ComPtr<IBuffer> outBuffer;
    REQUIRE_CALL(device->createBuffer(bufferDesc, initialData, outBuffer.writeRef()));
    return outBuffer;
}

GPU_TEST_CASE("texture-shared-cuda", D3D12 | Vulkan | DontCreateDevice)
{
    if (!isDeviceTypeAvailable(DeviceType::CUDA))
        SKIP("CUDA not available");

    ComPtr<IDevice> srcDevice = createTestingDevice(ctx, ctx->deviceType);
    ComPtr<IDevice> dstDevice = createTestingDevice(ctx, DeviceType::CUDA);

    if (srcDevice->getInfo().adapterLUID != dstDevice->getInfo().adapterLUID)
        SKIP("Devices do not refer to the same physical device");

    SamplerDesc samplerDesc;
    auto sampler = dstDevice->createSampler(samplerDesc);

    float initFloatData[16] = {0.0f};
    auto floatResults = createBuffer<float>(dstDevice, 16, initFloatData);

    uint32_t initUintData[16] = {0u};
    auto uintResults = createBuffer<uint32_t>(dstDevice, 16, initUintData);

    int32_t initIntData[16] = {0};
    auto intResults = createBuffer<uint32_t>(dstDevice, 16, initIntData);

    Extent3D size = {};
    size.width = 2;
    size.height = 2;
    size.depth = 1;

    {
        float texData[] =
            {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.5f, 0.5f, 0.5f, 1.0f};
        SubresourceData subData = {(void*)texData, 32, 0};

        // Create a shareable texture using srcDevice, get its handle, then create a texture using the handle using
        // dstDevice. Read back the texture and check that its contents are correct.
        auto srcTexture = createTexture(srcDevice, size, Format::RGBA32Float, &subData);

        // Wait for the texture to be filled in before reading it back
        srcDevice->getQueue(QueueType::Graphics)->waitOnHost();

        NativeHandle sharedHandle;
        REQUIRE_CALL(srcTexture->getSharedHandle(&sharedHandle));
        ComPtr<ITexture> dstTexture;
        size_t sizeInBytes = 0;
        size_t alignment = 0;
        REQUIRE_CALL(srcDevice->getTextureAllocationInfo(srcTexture->getDesc(), &sizeInBytes, &alignment));
        REQUIRE_CALL(
            dstDevice
                ->createTextureFromSharedHandle(sharedHandle, srcTexture->getDesc(), sizeInBytes, dstTexture.writeRef())
        );
        // Reading back the buffer from srcDevice to make sure it's been filled in before reading anything back from
        // dstDevice
        compareComputeResult(dstDevice, dstTexture, 0, 0, span(texData, texData + 16));

        setUpAndRunShader(dstDevice, dstTexture, floatResults, "copyTexFloat4");
        compareComputeResult(
            dstDevice,
            floatResults,
            makeArray<
                float>(1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.5f, 0.5f, 0.5f, 1.0f)
        );
    }
}
#endif
