#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

#if SLANG_RHI_ENABLE_AFTERMATH

GPU_TEST_CASE("aftermath-tdr", D3D11 | D3D12 | Vulkan | DontCreateDevice)
{
    SKIP("manual test only");

    DeviceExtraOptions extraOptions = {};
    extraOptions.debugDeviceOptions = DebugDeviceOptions::Aftermath;
    extraOptions.compilerOptions.push_back(
        slang::CompilerOptionEntry{
            slang::CompilerOptionName::DebugInformation,
            {
                slang::CompilerOptionValueKind::Int,
                SLANG_DEBUG_INFO_LEVEL_MAXIMAL,
            },
        }
    );
    device = createTestingDevice(ctx, ctx->deviceType, false, &extraOptions);

    ComPtr<IShaderProgram> shaderProgram;
    REQUIRE_CALL(loadProgram(device, "test-aftermath-tdr", "computeMain", shaderProgram.writeRef()));

    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

    ComPtr<IBuffer> buffer;
    {
        BufferDesc desc = {};
        desc.size = 1024;
        desc.usage = BufferUsage::UnorderedAccess | BufferUsage::CopySource;
        desc.label = "Test Buffer";
        REQUIRE_CALL(device->createBuffer(desc, nullptr, buffer.writeRef()));
    }

    {
        auto queue = device->getQueue(QueueType::Graphics);
        for (int i = 0; i < 3; ++i)
        {
            auto commandEncoder = queue->createCommandEncoder();
            auto passEncoder = commandEncoder->beginComputePass();
            IShaderObject* shaderObject = passEncoder->bindPipeline(pipeline);
            ShaderCursor cursor(shaderObject->getEntryPoint(0));
            cursor["buffer"].setBinding(buffer);
            passEncoder->pushDebugGroup("debug group 1", {1.f, 1.f, 1.f});
            passEncoder->pushDebugGroup("debug group 2", {1.f, 1.f, 1.f});
            passEncoder->dispatchCompute(1, 1, 1);
            passEncoder->popDebugGroup();
            passEncoder->popDebugGroup();
            passEncoder->end();
            REQUIRE_CALL(queue->submit(commandEncoder->finish()));
            REQUIRE_CALL(queue->waitOnHost());
        }
    }
}

#endif
