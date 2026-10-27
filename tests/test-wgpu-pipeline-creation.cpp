#include "testing.h"

#if SLANG_RHI_ENABLE_WGPU
#include "core/deferred.h"
#include "wgpu/wgpu-device.h"

#include <atomic>

using namespace rhi;
using namespace rhi::testing;

// Pipeline creation goes through the async entry point. Deferred pipelines resolved by finish() use the
// synchronous one, since finish() must not wait.
GPU_TEST_CASE("wgpu-pipeline-creation-entry-points", WGPU)
{
    ComPtr<IShaderProgram> program;
    REQUIRE_CALL(loadProgram(device, "test-compute-trivial", "computeMain", program.writeRef()));

    auto& api = checked_cast<wgpu::DeviceImpl*>(getUnderlyingDevice(device))->m_ctx.api;
    static WGPUProcDeviceCreateComputePipeline originalCreate;
    static WGPUProcDeviceCreateComputePipelineAsync originalCreateAsync;
    static std::atomic<uint32_t> syncCount;
    static std::atomic<uint32_t> asyncCount;
    originalCreate = api.wgpuDeviceCreateComputePipeline;
    originalCreateAsync = api.wgpuDeviceCreateComputePipelineAsync;
    syncCount = 0;
    asyncCount = 0;
    SLANG_RHI_DEFERRED({
        api.wgpuDeviceCreateComputePipeline = originalCreate;
        api.wgpuDeviceCreateComputePipelineAsync = originalCreateAsync;
    });
    api.wgpuDeviceCreateComputePipeline = [](WGPUDevice device, const WGPUComputePipelineDescriptor* desc)
    {
        ++syncCount;
        return originalCreate(device, desc);
    };
    api.wgpuDeviceCreateComputePipelineAsync = [](WGPUDevice device,
                                                  const WGPUComputePipelineDescriptor* desc,
                                                  WGPUCreateComputePipelineAsyncCallbackInfo callbackInfo)
    {
        ++asyncCount;
        return originalCreateAsync(device, desc, callbackInfo);
    };

    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = program;
    pipelineDesc.compilationPolicy = PipelineCompilationPolicy::Immediate;
    ComPtr<IComputePipeline> immediatePipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, immediatePipeline.writeRef()));
    CHECK(asyncCount == 1);
    CHECK(syncCount == 0);

    pipelineDesc.compilationPolicy = PipelineCompilationPolicy::Deferred;
    ComPtr<IComputePipeline> deferredPipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, deferredPipeline.writeRef()));
    CHECK(asyncCount == 1);
    CHECK(syncCount == 0);

    float initialData[] = {0.0f, 1.0f, 2.0f, 3.0f};
    BufferDesc bufferDesc = {};
    bufferDesc.size = sizeof(initialData);
    bufferDesc.elementSize = sizeof(float);
    bufferDesc.usage = BufferUsage::UnorderedAccess | BufferUsage::CopyDestination | BufferUsage::CopySource;
    ComPtr<IBuffer> buffer;
    REQUIRE_CALL(device->createBuffer(bufferDesc, initialData, buffer.writeRef()));

    auto queue = device->getQueue(QueueType::Graphics);
    auto encoder = queue->createCommandEncoder();
    for (IComputePipeline* pipeline : {immediatePipeline.get(), deferredPipeline.get()})
    {
        auto pass = encoder->beginComputePass();
        ShaderCursor cursor(pass->bindPipeline(pipeline));
        cursor["buffer"].setBinding(buffer);
        cursor["value"].setData(10.f);
        pass->dispatchCompute(1, 1, 1);
        pass->end();
    }
    ComPtr<ICommandBuffer> commandBuffer;
    REQUIRE_CALL(encoder->finish(commandBuffer.writeRef()));
    CHECK(asyncCount == 1);
    CHECK(syncCount == 1);
    REQUIRE_CALL(queue->submit(commandBuffer));
    REQUIRE_CALL(queue->waitOnHost());

    compareComputeResult(device, buffer, makeArray<float>(22.0f, 23.0f, 24.0f, 25.0f));
}
#endif
