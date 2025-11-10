#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

#if SLANG_RHI_ENABLE_NVAPI

GPU_TEST_CASE("nvapi-implicit", D3D12 | DontCreateDevice)
{
    device = createTestingDevice(ctx, ctx->deviceType, true);
    if (!device->hasCapability(Capability::hlsl_nvapi))
        SKIP("Device does not support NVAPI");

    ComPtr<IShaderProgram> shaderProgram;
    REQUIRE_CALL(loadProgram(device, "test-nvapi-implicit", "computeMain", shaderProgram.writeRef()));

    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

    uint32_t globalVar = 1000;

    ComPtr<IBuffer> globalBuffer;
    {
        uint32_t initialData[] = {2000};
        BufferDesc desc = {};
        desc.size = 4;
        desc.usage = BufferUsage::ShaderResource | BufferUsage::CopyDestination;
        REQUIRE_CALL(device->createBuffer(desc, initialData, globalBuffer.writeRef()));
    }

    ComPtr<IBuffer> buffer;
    {
        uint32_t initialData[] = {3000};
        BufferDesc desc = {};
        desc.size = 4;
        desc.usage = BufferUsage::ShaderResource | BufferUsage::CopyDestination;
        REQUIRE_CALL(device->createBuffer(desc, initialData, buffer.writeRef()));
    }

    ComPtr<IBuffer> result;
    {
        BufferDesc desc = {};
        desc.size = 16;
        desc.usage = BufferUsage::UnorderedAccess | BufferUsage::CopySource;
        REQUIRE_CALL(device->createBuffer(desc, nullptr, result.writeRef()));
    }

    {
        auto queue = device->getQueue(QueueType::Graphics);
        auto commandEncoder = queue->createCommandEncoder();
        auto passEncoder = commandEncoder->beginComputePass();
        IShaderObject* rootObject = passEncoder->bindPipeline(pipeline);
        ShaderCursor globalCursor(rootObject);
        globalCursor["globalVar"].setData(globalVar);
        globalCursor["globalBuffer"].setBinding(globalBuffer);
        ShaderCursor entryPointCursor(rootObject->getEntryPoint(0));
        entryPointCursor["buffer"].setBinding(buffer);
        entryPointCursor["result"].setBinding(result);
        passEncoder->dispatchCompute(1, 1, 1);
        passEncoder->end();

        queue->submit(commandEncoder->finish());
        queue->waitOnHost();
    }

    compareComputeResult(device, result, std::array{1000, 2000, 3000});
}

GPU_TEST_CASE("nvapi-explicit", D3D12 | DontCreateDevice)
{
    DeviceExtraOptions extraOptions;
    const char* nvapiSearchPath = SLANG_RHI_NVAPI_INCLUDE_DIR;
    extraOptions.searchPaths.push_back(nvapiSearchPath);
    device = createTestingDevice(ctx, ctx->deviceType, false, &extraOptions);
    if (!device->hasCapability(Capability::hlsl_nvapi))
        SKIP("Device does not support NVAPI");

    ComPtr<IShaderProgram> shaderProgram;
    REQUIRE_CALL(loadProgram(device, "test-nvapi-explicit", "computeMain", shaderProgram.writeRef()));

    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

    uint32_t globalVar = 1000;

    ComPtr<IBuffer> globalBuffer;
    {
        uint32_t initialData[] = {2000};
        BufferDesc desc = {};
        desc.size = 4;
        desc.usage = BufferUsage::ShaderResource | BufferUsage::CopyDestination;
        REQUIRE_CALL(device->createBuffer(desc, initialData, globalBuffer.writeRef()));
    }

    ComPtr<IBuffer> buffer;
    {
        uint32_t initialData[] = {3000};
        BufferDesc desc = {};
        desc.size = 4;
        desc.usage = BufferUsage::ShaderResource | BufferUsage::CopyDestination;
        REQUIRE_CALL(device->createBuffer(desc, initialData, buffer.writeRef()));
    }

    ComPtr<IBuffer> result;
    {
        BufferDesc desc = {};
        desc.size = 16;
        desc.usage = BufferUsage::UnorderedAccess | BufferUsage::CopySource;
        REQUIRE_CALL(device->createBuffer(desc, nullptr, result.writeRef()));
    }

    {
        auto queue = device->getQueue(QueueType::Graphics);
        auto commandEncoder = queue->createCommandEncoder();
        auto passEncoder = commandEncoder->beginComputePass();
        IShaderObject* rootObject = passEncoder->bindPipeline(pipeline);
        ShaderCursor globalCursor(rootObject);
        globalCursor["globalVar"].setData(globalVar);
        globalCursor["globalBuffer"].setBinding(globalBuffer);
        ShaderCursor entryPointCursor(rootObject->getEntryPoint(0));
        entryPointCursor["buffer"].setBinding(buffer);
        entryPointCursor["result"].setBinding(result);
        passEncoder->dispatchCompute(1, 1, 1);
        passEncoder->end();

        queue->submit(commandEncoder->finish());
        queue->waitOnHost();
    }

    compareComputeResult(device, result, std::array{1000, 2000, 3000});
}

#endif
