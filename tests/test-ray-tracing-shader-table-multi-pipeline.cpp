#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

// Test verifies that a shader table works correctly when used with multiple
// pipelines that have the same entry point names but different entry point
// parameters.
GPU_TEST_CASE("ray-tracing-shader-table-multi-pipeline", ALL)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");

    if (device->getDeviceType() == DeviceType::Vulkan)
        SKIP("Vulkan does not handle entry point parameters correctly yet");

    if (device->getDeviceType() == DeviceType::CUDA)
        SKIP("CUDA/OptiX uses __ldg to load entrypoint parameters which uses non-coherent read-only data cache");

    // Pipeline A: rayGen(output, value) -> output[i] = value + i
    ComPtr<IShaderProgram> programA;
    REQUIRE_CALL(loadProgram(device, "test-ray-tracing-shader-table-multi-pipeline-a", "rayGen", programA.writeRef()));

    ComPtr<IRayTracingPipeline> pipelineA;
    {
        RayTracingPipelineDesc pipelineDesc = {};
        pipelineDesc.program = programA;
        REQUIRE_CALL(device->createRayTracingPipeline(pipelineDesc, pipelineA.writeRef()));
    }

    // Pipeline B: rayGen(output, value, multiplier) -> output[i] = value + i * multiplier
    ComPtr<IShaderProgram> programB;
    REQUIRE_CALL(loadProgram(device, "test-ray-tracing-shader-table-multi-pipeline-b", "rayGen", programB.writeRef()));

    ComPtr<IRayTracingPipeline> pipelineB;
    {
        RayTracingPipelineDesc pipelineDesc = {};
        pipelineDesc.program = programB;
        REQUIRE_CALL(device->createRayTracingPipeline(pipelineDesc, pipelineB.writeRef()));
    }

    // Create a single shader table with the "rayGen" entry point.
    // This shader table will be used with both pipelines.
    ComPtr<IShaderTable> shaderTable;
    {
        ShaderTableDesc shaderTableDesc = {};
        const char* rayGenNames[] = {"rayGen"};
        shaderTableDesc.rayGenShaderCount = SLANG_COUNT_OF(rayGenNames);
        shaderTableDesc.rayGenShaderEntryPointNames = rayGenNames;
        REQUIRE_CALL(device->createShaderTable(shaderTableDesc, shaderTable.writeRef()));
    }

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

    // --- Use pipeline B first ---
    // This populates the shader table's per-pipeline data for pipeline B.
    {
        auto commandEncoder = queue->createCommandEncoder();
        auto passEncoder = commandEncoder->beginRayTracingPass();
        auto rootObject = passEncoder->bindPipeline(pipelineB, shaderTable);
        auto cursor = ShaderCursor(rootObject->getEntryPoint(0));
        cursor["output"].setBinding(outputBuffer);
        cursor["value"].setData<uint32_t>(100);
        cursor["multiplier"].setData<uint32_t>(3);
        passEncoder->dispatchRays(0, width, height, 1);
        passEncoder->end();
        queue->submit(commandEncoder->finish());
    }

    // rayGen B: output[i] = value + i * multiplier = 100 + i * 3
    compareComputeResult(device, outputBuffer, std::array<uint32_t, 4>{100, 103, 106, 109});

    // --- Now use pipeline A ---
    // Before the fix, this would use the strides/offsets computed for pipeline B,
    // leading to incorrect shader record addressing.
    {
        auto commandEncoder = queue->createCommandEncoder();
        auto passEncoder = commandEncoder->beginRayTracingPass();
        auto rootObject = passEncoder->bindPipeline(pipelineA, shaderTable);
        auto cursor = ShaderCursor(rootObject->getEntryPoint(0));
        cursor["output"].setBinding(outputBuffer);
        cursor["value"].setData<uint32_t>(1);
        passEncoder->dispatchRays(0, width, height, 1);
        passEncoder->end();
        queue->submit(commandEncoder->finish());
    }

    // rayGen A: output[i] = value + i = 1 + i
    compareComputeResult(device, outputBuffer, std::array<uint32_t, 4>{1, 2, 3, 4});

    // --- Go back to pipeline B ---
    // Verify pipeline B still works correctly after using pipeline A.
    {
        auto commandEncoder = queue->createCommandEncoder();
        auto passEncoder = commandEncoder->beginRayTracingPass();
        auto rootObject = passEncoder->bindPipeline(pipelineB, shaderTable);
        auto cursor = ShaderCursor(rootObject->getEntryPoint(0));
        cursor["output"].setBinding(outputBuffer);
        cursor["value"].setData<uint32_t>(50);
        cursor["multiplier"].setData<uint32_t>(10);
        passEncoder->dispatchRays(0, width, height, 1);
        passEncoder->end();
        queue->submit(commandEncoder->finish());
    }

    // rayGen B: output[i] = value + i * multiplier = 50 + i * 10
    compareComputeResult(device, outputBuffer, std::array<uint32_t, 4>{50, 60, 70, 80});
}
