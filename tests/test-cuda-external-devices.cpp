#include "testing.h"

#if SLANG_RHI_ENABLE_CUDA

#include <random>
#include "../src/core/span.h"
#include "../src/cuda/cuda-device.h"
#include "../src/cuda/cuda-api.h"
#include "../src/cuda/cuda-utils.h"
#include "debug-layer/debug-device.h"

using namespace rhi;
using namespace rhi::testing;

rhi::cuda::DeviceImpl* getCUDADevice(IDevice* device)
{
    if (auto debugDevice = dynamic_cast<debug::DebugDevice*>(device))
        return (rhi::cuda::DeviceImpl*)debugDevice->baseObject.get();
    else
        return (rhi::cuda::DeviceImpl*)device;
}

void runPointerCopyTest(rhi::cuda::DeviceImpl* device, CUstream stream, bool expect_fail_to_copy)
{
    SLANG_CUDA_CTX_SCOPE(device);
    ComPtr<IShaderProgram> shaderProgram;
    REQUIRE_CALL(loadProgram(device, "test-pointer-copy", "computeMain", shaderProgram.writeRef()));

    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

    const int numberCount = 4096;

    // Generate random data for 'numberCount' uint32s
    std::vector<uint8_t> data;
    std::mt19937 rng(124112);
    std::uniform_int_distribution<int> dist(0, 255);
    data.resize(numberCount * 4);
    for (auto& byte : data)
        byte = (uint8_t)dist(rng);

    // Setup buffer descriptor
    BufferDesc bufferDesc = {};
    bufferDesc.size = numberCount * sizeof(uint32_t);
    bufferDesc.format = Format::Undefined;
    bufferDesc.elementSize = sizeof(uint32_t);
    bufferDesc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess | BufferUsage::CopyDestination |
                       BufferUsage::CopySource;
    bufferDesc.defaultState = ResourceState::UnorderedAccess;
    bufferDesc.memoryType = MemoryType::DeviceLocal;

    // Create source buffer
    ComPtr<IBuffer> src;
    REQUIRE_CALL(device->createBuffer(bufferDesc, data.data(), src.writeRef()));

    // Create dest buffer initialized to zeros
    std::vector<uint8_t> zeros;
    zeros.resize(numberCount * 4);
    memset(zeros.data(), 0, zeros.size());
    ComPtr<IBuffer> dst;
    REQUIRE_CALL(device->createBuffer(bufferDesc, zeros.data(), dst.writeRef()));


    // We have done all the set up work, now it is time to start recording a command buffer for
    // GPU execution.
    {
        ComPtr<ICommandQueue> queue;
        REQUIRE_CALL(device->getQueue(QueueType::Graphics, queue.writeRef()));

        auto commandEncoder = queue->createCommandEncoder();

        auto passEncoder = commandEncoder->beginComputePass();
        auto rootObject = passEncoder->bindPipeline(pipeline);
        ShaderCursor shaderCursor(rootObject);
        shaderCursor["src"].setData(src->getDeviceAddress());
        shaderCursor["dst"].setData(dst->getDeviceAddress());

        passEncoder->dispatchCompute(numberCount / 32, 1, 1);
        passEncoder->end();

        ComPtr<ICommandBuffer> cb = commandEncoder->finish();

        ICommandBuffer* buffers[1] = {cb.get()};

        SubmitDesc desc = {};
        desc.commandBuffers = buffers;
        desc.commandBufferCount = 1;
        desc.cudaStream = stream;

        // Unfortunately our command executor can't return errors, and instead throws an
        // assert when this CUDA command fails. To avoid killing the test even when things
        // go wrong, we disable asserts for the scope of this call. If the submit fails,
        // the result comparison should detect the real error.
        {
            SLANG_RHI_DISABLE_ASSERT_SCOPE();
            queue->submit(desc);
        }

        queue->waitOnHost();
    }

    if (!expect_fail_to_copy)
        compareComputeResult(device, dst, span<uint8_t>(data));
    else
        compareComputeResult(device, dst, span<uint8_t>(zeros));
}

GPU_TEST_CASE("cuda-external-device", CUDA)
{
    using namespace rhi::cuda;

    // Get CUDA implementations of the main test device
    auto cuda_device_1 = getCUDADevice(device);

    // Explicitly create a 2nd context, and pop it off the stack so it doesn't become the current context.
    CUcontext tmp_context;
    cuCtxCreate(&tmp_context, 0, cuda_device_1->m_ctx.device);
    CUcontext orig;
    cuCtxPopCurrent(&orig);

    // Create a 2nd external device using the new context.
    GpuTestContext ctx2;
    ctx2.slangGlobalSession = getSlangGlobalSession();
    DeviceExtraOptions opts;
    opts.existingDeviceHandles.handles[0].type = NativeHandleType::CUcontext;
    opts.existingDeviceHandles.handles[0].value = reinterpret_cast<uint64_t>(tmp_context);
    ComPtr<IDevice> device2 = createTestingDevice(&ctx2, DeviceType::CUDA, false, &opts);

    // Get CUDA implementations of both devices
    auto cuda_device_2 = getCUDADevice(device2.get());

    // Create a 3rd device that shares context with the 1st
    GpuTestContext ctx3;
    opts.existingDeviceHandles.handles[0].type = NativeHandleType::CUcontext;
    opts.existingDeviceHandles.handles[0].value = reinterpret_cast<uint64_t>(cuda_device_1->m_ctx.context);
    ctx3.slangGlobalSession = getSlangGlobalSession();
    ComPtr<IDevice> device3 = createTestingDevice(&ctx3, DeviceType::CUDA, false, &opts);
    auto cuda_device_3 = getCUDADevice(device3.get());

    // Perform initial verification by just running the copy test on all devices
    runPointerCopyTest(cuda_device_1, nullptr, false);
    runPointerCopyTest(cuda_device_2, nullptr, false);
    runPointerCopyTest(cuda_device_3, nullptr, false);

    // Now use CUDA driver api to create a new stream from device1's context
    CUstream stream;
    {
        SLANG_CUDA_CTX_SCOPE(cuda_device_1);
        SLANG_CUDA_ASSERT_ON_FAIL(cuStreamCreate(&stream, 0));
    }

    // Now attempt and succeed in submitting on device 1 using the custom stream
    runPointerCopyTest(cuda_device_1, stream, false);

    // Now attempt and expect failure in submitting on device 2 using the custom stream,
    // because CUDA requires that the stream used is associated with the active
    // context.
    runPointerCopyTest(cuda_device_2, stream, true);

    // Now attempt and succeed in submitting on device 3 using the custom stream,
    // which should succeed as they're sharing the context.
    runPointerCopyTest(cuda_device_3, stream, false);

    // Clear our refs so devices get cleaned up if need be
    cuda_device_1 = nullptr;
    cuda_device_2 = nullptr;
    cuda_device_3 = nullptr;
    device2.setNull();
    device3.setNull();

    // Clean up CUDA!
    cuStreamDestroy(stream);
    cuCtxDestroy(tmp_context);
}
#endif
