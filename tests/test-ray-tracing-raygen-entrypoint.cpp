#include "testing.h"
#include <cstdio>

using namespace rhi;
using namespace rhi::testing;

// Resource-only entry points have no implicit ordinary-data wrapper. Explicit
// parameter groups must still retain their own ordinary data and descriptor sets.
GPU_TEST_CASE("ray-tracing-raygen-entrypoint-resources", ALL)
{
    REQUIRE_RAY_TRACING_SUPPORT(device);

    for (const char* entryPoint : {"rayGenResources", "rayGenConstantBuffer", "rayGenParameterBlock", "rayGenOffsets"})
    {
        CAPTURE(std::string(entryPoint));
        auto phase = [&](const char* name)
        {
            std::fprintf(stderr, "[raygen] variant=%s phase=%s\n", entryPoint, name);
            std::fflush(stderr);
        };
        phase("begin");
        {
            const bool hasGlobals = strcmp(entryPoint, "rayGenOffsets") == 0;
            const bool hasParameterGroup = !hasGlobals && strcmp(entryPoint, "rayGenResources") != 0;

            ComPtr<IShaderProgram> program;
            REQUIRE_CALL(loadProgram(
                device,
                hasGlobals ? "test-ray-tracing-raygen-entrypoint-offsets" : "test-ray-tracing-raygen-entrypoint",
                entryPoint,
                program.writeRef()
            ));
            phase("program-created");

            RayTracingPipelineDesc pipelineDesc = {};
            pipelineDesc.program = program;
            auto pipeline = device->createRayTracingPipeline(pipelineDesc);
            REQUIRE(pipeline != nullptr);
            phase("pipeline-created");

            ShaderTableDesc shaderTableDesc = {};
            shaderTableDesc.program = program;
            shaderTableDesc.rayGenShaderCount = 1;
            shaderTableDesc.rayGenShaderEntryPointNames = &entryPoint;
            ComPtr<IShaderTable> shaderTable;
            REQUIRE_CALL(device->createShaderTable(shaderTableDesc, shaderTable.writeRef()));

            BufferDesc bufferDesc = {};
            bufferDesc.size = (hasGlobals ? 2 : 1) * sizeof(uint32_t);
            bufferDesc.usage = BufferUsage::UnorderedAccess | BufferUsage::CopySource;
            const uint32_t initialValues[] = {99, 99};
            auto outputBuffer = device->createBuffer(bufferDesc, initialValues);
            REQUIRE(outputBuffer != nullptr);
            phase("resources-created");

            auto queue = device->getQueue(QueueType::Graphics);
            REQUIRE(queue != nullptr);
            auto commandEncoder = queue->createCommandEncoder();
            REQUIRE(commandEncoder != nullptr);
            auto passEncoder = commandEncoder->beginRayTracingPass();
            REQUIRE(passEncoder != nullptr);
            auto rootObject = passEncoder->bindPipeline(pipeline, shaderTable);
            REQUIRE(rootObject != nullptr);
            auto entryPointObject = rootObject->getEntryPoint(0);
            REQUIRE(entryPointObject != nullptr);
            auto cursor = ShaderCursor(entryPointObject);
            REQUIRE(cursor.isValid());
            entryPointObject.setNull();
            phase("root-bound");
            if (hasGlobals)
            {
                // Global ordinary data and a resource move the entry point's descriptor base.
                REQUIRE(ShaderCursor(rootObject)["bias"].isValid());
                REQUIRE(ShaderCursor(rootObject)["globalOutput"].isValid());
                REQUIRE(cursor["value"].isValid());
                REQUIRE_CALL(ShaderCursor(rootObject)["bias"].setData<uint32_t>(45));
                REQUIRE_CALL(ShaderCursor(rootObject)["globalOutput"].setBinding(outputBuffer));
                REQUIRE_CALL(cursor["value"].setData<uint32_t>(12300));
            }
            if (hasParameterGroup)
            {
                cursor = cursor["params"];
                REQUIRE(cursor.isValid());
                REQUIRE(cursor["value"].isValid());
                REQUIRE_CALL(cursor["value"].setData<uint32_t>(12345));
            }
            REQUIRE(cursor["output"].isValid());
            REQUIRE_CALL(cursor["output"].setBinding(outputBuffer));
            phase("parameters-bound");
            passEncoder->dispatchRays(0, 1, 1, 1);
            passEncoder->end();
            phase("dispatch-recorded");
            auto commandBuffer = commandEncoder->finish();
            REQUIRE(commandBuffer != nullptr);
            phase("commands-finished");
            REQUIRE_CALL(queue->submit(commandBuffer));
            commandBuffer.setNull();
            phase("submitted");

            if (hasGlobals)
                compareComputeResult(device, outputBuffer, std::array<uint32_t, 2>{12345, 45});
            else
                compareComputeResult(device, outputBuffer, std::array<uint32_t, 1>{12345});
            phase("readback-complete");
        }
        phase("destroyed");
    }
}

// Test verifies that ray generation entry points can be selected correctly
// and entry point parameters are passed correctly.
GPU_TEST_CASE("ray-tracing-raygen-entrypoint", ALL)
{
    REQUIRE_RAY_TRACING_SUPPORT(device);

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
    REQUIRE_RAY_TRACING_SUPPORT(device);

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
