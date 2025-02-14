#include "testing.h"

#include <chrono>

using namespace rhi;
using namespace rhi::testing;

struct Shader
{
    ComPtr<IShaderProgram> program;
    slang::ProgramLayout* reflection = nullptr;
    ComputePipelineDesc pipelineDesc = {};
    ComPtr<IComputePipeline> pipeline;
};

GPU_TEST_CASE("benchmark-command", ALL)
{
    if (!device->hasFeature("parameter-block"))
        SKIP("no support for parameter blocks");

    Shader shader;
    REQUIRE_CALL(loadComputeProgram(device, shader.program, "test-benchmark-command", "addkernel", shader.reflection));
    shader.pipelineDesc.program = shader.program.get();
    REQUIRE_CALL(device->createComputePipeline(shader.pipelineDesc, shader.pipeline.writeRef()));

    float initialData[32];
    for (int i = 0; i < 32; ++i)
    {
        initialData[i] = (float)i;
    }
    BufferDesc bufferDesc = {};
    bufferDesc.size = 32 * sizeof(float);
    bufferDesc.format = Format::Unknown;
    bufferDesc.elementSize = sizeof(float);
    bufferDesc.memoryType = MemoryType::DeviceLocal;
    bufferDesc.usage = BufferUsage::ShaderResource | BufferUsage::CopyDestination;

    ComPtr<IBuffer> bufA;
    ComPtr<IBuffer> bufB;
    ComPtr<IBuffer> bufC;
    REQUIRE_CALL(device->createBuffer(bufferDesc, initialData, bufA.writeRef()));
    REQUIRE_CALL(device->createBuffer(bufferDesc, nullptr, bufB.writeRef()));
    bufferDesc.usage = BufferUsage::UnorderedAccess | BufferUsage::CopySource;
    REQUIRE_CALL(device->createBuffer(bufferDesc, nullptr, bufC.writeRef()));

    uint32_t DISPATCH_COUNT_PER_SUBMIT = 10000;
    uint32_t SUBMIT_COUNT = 1;
    // uint32_t DISPATCH_COUNT_PER_SUBMIT = 1;
    // uint32_t SUBMIT_COUNT = 100000;

    auto queue = device->getQueue(QueueType::Graphics);

    std::chrono::time_point<std::chrono::high_resolution_clock> start, end;

    {
        auto commandEncoder = queue->createCommandEncoder();

        for (uint32_t submitIndex = 0; submitIndex < SUBMIT_COUNT + 1; ++submitIndex)
        {
            if (submitIndex == 1)
            {
                start = std::chrono::high_resolution_clock::now();
            }

            for (uint32_t dispatchIndex = 0; dispatchIndex < DISPATCH_COUNT_PER_SUBMIT; ++dispatchIndex)
            {
                auto computePass = commandEncoder->beginComputePass();
                auto shaderObject = computePass->bindPipeline(shader.pipeline);

                uint32_t a = 1;
                uint32_t b = 2;

                ShaderCursor cursor(shaderObject);
                ShaderCursor block = cursor["addKernelData"];
                block["a"].setBinding(bufA);
                block["b"].setBinding(bufB);
                block["res"].setBinding(bufC);
                int count = 32;
                block["count"].setData(&count);

                computePass->dispatchCompute(1, 1, 1);
                computePass->end();
            }

            queue->submit(commandEncoder->finish());
            commandEncoder = queue->createCommandEncoder();
        }
    }

    end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    MESSAGE("Duration: " << duration << " ms");

    queue->waitOnHost();

    // compareComputeResult(device, outputBuffer, makeArray<float>(11.0f, 12.0f, 13.0f, 14.0f));
}
