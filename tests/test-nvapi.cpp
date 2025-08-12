#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

#if SLANG_RHI_ENABLE_NVAPI

TEST_CASE("nvapi-implicit")
{
    auto testFunc = [](GpuTestContext* ctx, DeviceType deviceType)
    {
        ComPtr<IDevice> device = createTestingDevice(ctx, deviceType, true);

        ComPtr<IShaderProgram> shaderProgram;
        slang::ProgramLayout* slangReflection = nullptr;
        REQUIRE_CALL(loadComputeProgram(device, shaderProgram, "test-nvapi-implicit", "computeMain", slangReflection));

        ComputePipelineDesc pipelineDesc = {};
        pipelineDesc.program = shaderProgram.get();
        ComPtr<IComputePipeline> pipeline;
        REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

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
            ShaderCursor cursor(rootObject->getEntryPoint(0));
            cursor["result"].setBinding(result);
            passEncoder->dispatchCompute(1, 1, 1);
            passEncoder->end();

            queue->submit(commandEncoder->finish());
            queue->waitOnHost();
        }

        compareComputeResult(device, result, std::array{1ul});
    };

    runGpuTests(testFunc, {DeviceType::D3D12});
}

TEST_CASE("nvapi-explicit")
{
    auto testFunc = [](GpuTestContext* ctx, DeviceType deviceType)
    {
        DeviceExtraOptions extraOptions;
        const char* nvapiSearchPath = SLANG_RHI_NVAPI_INCLUDE_DIR;
        extraOptions.searchPaths.push_back(nvapiSearchPath);
        ComPtr<IDevice> device = createTestingDevice(ctx, deviceType, false, &extraOptions);

        ComPtr<IShaderProgram> shaderProgram;
        slang::ProgramLayout* slangReflection = nullptr;
        REQUIRE_CALL(loadComputeProgram(device, shaderProgram, "test-nvapi-explicit", "computeMain", slangReflection));

        ComputePipelineDesc pipelineDesc = {};
        pipelineDesc.program = shaderProgram.get();
        ComPtr<IComputePipeline> pipeline;
        REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

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
            ShaderCursor cursor(rootObject->getEntryPoint(0));
            cursor["result"].setBinding(result);
            passEncoder->dispatchCompute(1, 1, 1);
            passEncoder->end();

            queue->submit(commandEncoder->finish());
            queue->waitOnHost();
        }

        compareComputeResult(device, result, std::array{1});
    };

    runGpuTests(testFunc, {DeviceType::D3D12});
}

#endif
