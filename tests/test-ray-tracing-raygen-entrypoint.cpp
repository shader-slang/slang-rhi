#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

// Test verifies that ray generation entry points can be selected correctly
// and entry point parameters are passed correctly.
GPU_TEST_CASE("ray-tracing-raygen-entrypoint", ALL)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");

    ComPtr<IShaderProgram> program;
    REQUIRE_CALL(loadProgram(device, "test-ray-tracing-raygen-entrypoint", {"rayGenA", "rayGenB"}, program.writeRef()));

    ComPtr<IRayTracingPipeline> pipeline;
    RayTracingPipelineDesc pipelineDesc = {};
    pipelineDesc.program = program;
    REQUIRE_CALL(device->createRayTracingPipeline(pipelineDesc, pipeline.writeRef()));

    ComPtr<IShaderTable> shaderTable;
    ShaderTableDesc shaderTableDesc = {};
    shaderTableDesc.program = program;
    const char* rayGenNames[] = {"rayGenA", "rayGenB"};
    shaderTableDesc.rayGenShaderCount = SLANG_COUNT_OF(rayGenNames);
    shaderTableDesc.rayGenShaderEntryPointNames = rayGenNames;
    REQUIRE_CALL(device->createShaderTable(shaderTableDesc, shaderTable.writeRef()));

    const uint32_t width = 2;
    const uint32_t height = 2;

    ComPtr<IBuffer> outputBuffer;
    {
        BufferDesc bufferDesc;
        bufferDesc.size = width * height * sizeof(uint32_t);
        bufferDesc.usage = BufferUsage::UnorderedAccess | BufferUsage::CopySource;
        outputBuffer = device->createBuffer(bufferDesc, nullptr);
        REQUIRE(outputBuffer != nullptr);
    }

    auto queue = device->getQueue(QueueType::Graphics);

    // Dispatch ray generation entry point A
    {
        auto commandEncoder = queue->createCommandEncoder();

        auto passEncoder = commandEncoder->beginRayTracingPass();
        auto rootObject = passEncoder->bindPipeline(pipeline, shaderTable);
        auto cursor = ShaderCursor(rootObject->getEntryPoint(0));
        cursor["output"].setBinding(outputBuffer);
        cursor["value"].setData<uint32_t>(1);
        passEncoder->dispatchRays(0, width, height, 1);
        passEncoder->end();

        queue->submit(commandEncoder->finish());
    }

    compareComputeResult(device, outputBuffer, std::array<uint32_t, 4>{1, 2, 3, 4});

    // Dispatch ray generation entry point B
    {
        auto commandEncoder = queue->createCommandEncoder();

        auto passEncoder = commandEncoder->beginRayTracingPass();
        auto rootObject = passEncoder->bindPipeline(pipeline, shaderTable);
        auto cursor = ShaderCursor(rootObject->getEntryPoint(1));
        cursor["output"].setBinding(outputBuffer);
        cursor["value"].setData<uint32_t>(10);
        passEncoder->dispatchRays(1, width, height, 1);
        passEncoder->end();

        queue->submit(commandEncoder->finish());
    }

    compareComputeResult(device, outputBuffer, std::array<uint32_t, 4>{10, 12, 14, 16});
}

// Same test as above but with different parameter values to verify
// that parameters are updated correctly on subsequent dispatches.
GPU_TEST_CASE("ray-tracing-raygen-entrypoint-2", ALL)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");

    if (device->getDeviceType() == DeviceType::CUDA)
        SKIP("CUDA/OptiX uses __ldg to load entrypoint parameters which uses non-coherent read-only data cache");

    ComPtr<IShaderProgram> program;
    REQUIRE_CALL(loadProgram(device, "test-ray-tracing-raygen-entrypoint", {"rayGenA", "rayGenB"}, program.writeRef()));

    ComPtr<IRayTracingPipeline> pipeline;
    RayTracingPipelineDesc pipelineDesc = {};
    pipelineDesc.program = program;
    REQUIRE_CALL(device->createRayTracingPipeline(pipelineDesc, pipeline.writeRef()));

    ComPtr<IShaderTable> shaderTable;
    ShaderTableDesc shaderTableDesc = {};
    shaderTableDesc.program = program;
    const char* rayGenNames[] = {"rayGenA", "rayGenB"};
    shaderTableDesc.rayGenShaderCount = SLANG_COUNT_OF(rayGenNames);
    shaderTableDesc.rayGenShaderEntryPointNames = rayGenNames;
    REQUIRE_CALL(device->createShaderTable(shaderTableDesc, shaderTable.writeRef()));

    const uint32_t width = 2;
    const uint32_t height = 2;

    ComPtr<IBuffer> outputBuffer;
    {
        BufferDesc bufferDesc;
        bufferDesc.size = width * height * sizeof(uint32_t);
        bufferDesc.usage = BufferUsage::UnorderedAccess | BufferUsage::CopySource;
        outputBuffer = device->createBuffer(bufferDesc, nullptr);
        REQUIRE(outputBuffer != nullptr);
    }

    auto queue = device->getQueue(QueueType::Graphics);

    // Dispatch ray generation entry point A
    {
        auto commandEncoder = queue->createCommandEncoder();

        auto passEncoder = commandEncoder->beginRayTracingPass();
        auto rootObject = passEncoder->bindPipeline(pipeline, shaderTable);
        auto cursor = ShaderCursor(rootObject->getEntryPoint(0));
        cursor["output"].setBinding(outputBuffer);
        cursor["value"].setData<uint32_t>(1);
        passEncoder->dispatchRays(0, width, height, 1);
        passEncoder->end();

        queue->submit(commandEncoder->finish());
    }

    compareComputeResult(device, outputBuffer, std::array<uint32_t, 4>{1, 2, 3, 4});

    // Dispatch ray generation entry point B
    {
        auto commandEncoder = queue->createCommandEncoder();

        auto passEncoder = commandEncoder->beginRayTracingPass();
        auto rootObject = passEncoder->bindPipeline(pipeline, shaderTable);
        auto cursor = ShaderCursor(rootObject->getEntryPoint(1));
        cursor["output"].setBinding(outputBuffer);
        cursor["value"].setData<uint32_t>(10);
        passEncoder->dispatchRays(1, width, height, 1);
        passEncoder->end();

        queue->submit(commandEncoder->finish());
    }

    compareComputeResult(device, outputBuffer, std::array<uint32_t, 4>{10, 12, 14, 16});

    // --- Round 2: dispatch again with different values ---

    // Dispatch ray generation entry point A with value=100
    {
        auto commandEncoder = queue->createCommandEncoder();

        auto passEncoder = commandEncoder->beginRayTracingPass();
        auto rootObject = passEncoder->bindPipeline(pipeline, shaderTable);
        auto cursor = ShaderCursor(rootObject->getEntryPoint(0));
        cursor["output"].setBinding(outputBuffer);
        cursor["value"].setData<uint32_t>(100);
        passEncoder->dispatchRays(0, width, height, 1);
        passEncoder->end();

        queue->submit(commandEncoder->finish());
    }

    // rayGenA: output[i] = value + i = 100 + i
    compareComputeResult(device, outputBuffer, std::array<uint32_t, 4>{100, 101, 102, 103});

    // Dispatch ray generation entry point B with value=200
    {
        auto commandEncoder = queue->createCommandEncoder();

        auto passEncoder = commandEncoder->beginRayTracingPass();
        auto rootObject = passEncoder->bindPipeline(pipeline, shaderTable);
        auto cursor = ShaderCursor(rootObject->getEntryPoint(1));
        cursor["output"].setBinding(outputBuffer);
        cursor["value"].setData<uint32_t>(200);
        passEncoder->dispatchRays(1, width, height, 1);
        passEncoder->end();

        queue->submit(commandEncoder->finish());
    }

    // rayGenB: output[i] = value + i * 2 = 200 + i * 2
    compareComputeResult(device, outputBuffer, std::array<uint32_t, 4>{200, 202, 204, 206});
}
