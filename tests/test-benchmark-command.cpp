#include "testing.h"
#include "memory-report.h"
#include "../src/parameter-block-cache.h"
#include "../src/core/arena-allocator.h"

#include <chrono>
#include <cinttypes>

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
    if (!device->hasFeature(Feature::ParameterBlock))
        SKIP("no support for parameter blocks");

    Shader shader;
    REQUIRE_CALL(
        loadAndLinkProgram(device, "test-benchmark-command", "addkernel", shader.program.writeRef(), &shader.reflection)
    );
    shader.pipelineDesc.program = shader.program.get();
    REQUIRE_CALL(device->createComputePipeline(shader.pipelineDesc, shader.pipeline.writeRef()));

    float initialData[32];
    for (int i = 0; i < 32; ++i)
    {
        initialData[i] = (float)i;
    }
    BufferDesc bufferDesc = {};
    bufferDesc.size = 32 * sizeof(float);
    bufferDesc.format = Format::Undefined;
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
    int64_t duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    fprintf(stderr, ": Duration: %" PRIi64 " ms)", duration);

    queue->waitOnHost();

    // compareComputeResult(device, outputBuffer, makeArray<float>(11.0f, 12.0f, 13.0f, 14.0f));
}

GPU_TEST_CASE("benchmark-parameter-block-cache", ALL)
{
    if (!device->hasFeature(Feature::ParameterBlock))
        SKIP("no support for parameter blocks");

    Shader shaders[2];
    for (uint32_t i = 0; i < 2; ++i)
    {
        REQUIRE_CALL(loadAndLinkProgram(
            device,
            "test-benchmark-parameter-block",
            i ? "computeB" : "computeA",
            shaders[i].program.writeRef(),
            &shaders[i].reflection
        ));
        shaders[i].pipelineDesc.program = shaders[i].program;
        REQUIRE_CALL(device->createComputePipeline(shaders[i].pipelineDesc, shaders[i].pipeline.writeRef()));
    }
    float value = 1.f;
    BufferDesc bufferDesc;
    bufferDesc.size = sizeof(value);
    bufferDesc.elementSize = sizeof(value);
    bufferDesc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess | BufferUsage::CopySource |
                       BufferUsage::CopyDestination;
    auto input = device->createBuffer(bufferDesc, &value);
    auto output = device->createBuffer(bufferDesc, &value);
    REQUIRE(input);
    REQUIRE(output);
    auto queue = device->getQueue(QueueType::Graphics);
    constexpr uint32_t dispatchCount = 512;
    std::vector<float> constants(4096, 2.f);

    struct CaptureAllocations
    {
        ArenaAllocator::AllocationStats* previous;
        CaptureAllocations(ArenaAllocator::AllocationStats& stats)
            : previous(ArenaAllocator::allocationStats)
        {
            ArenaAllocator::allocationStats = &stats;
        }
        ~CaptureAllocations() { ArenaAllocator::allocationStats = previous; }
    };
    using Clock = std::chrono::steady_clock;
    auto milliseconds = [](auto a, auto b)
    {
        return std::chrono::duration<double, std::milli>(b - a).count();
    };

    for (bool alternating : {false, true})
    {
        for (uint32_t distinctCount : {1u, 64u, dispatchCount})
        {
            for (bool finalized : {false, true})
            {
                std::vector<ComPtr<IShaderObject>> blocks;
                for (uint32_t i = 0; i < distinctCount; ++i)
                {
                    auto block = device->createShaderObject(
                        shaders[0].reflection->findTypeByName("BenchmarkScene"),
                        ShaderObjectContainerType::None
                    );
                    REQUIRE(block);
                    ShaderCursor cursor(block);
                    REQUIRE_CALL(cursor["constants"].setData(constants.data(), constants.size() * sizeof(float)));
                    for (const char* name : {"input0", "input1", "input2", "input3"})
                        REQUIRE_CALL(cursor[name].setBinding(input));
                    if (finalized)
                    {
                        REQUIRE_CALL(block->finalize());
                    }
                    blocks.push_back(block);
                }
                ComPtr<IShaderObject> roots[2];
                for (uint32_t i = 0; i < 2; ++i)
                {
                    roots[i] = device->createRootShaderObject(shaders[i].program);
                    REQUIRE_CALL(ShaderCursor(roots[i])["output"].setBinding(output));
                    REQUIRE_CALL(ShaderCursor(roots[i])["scene"].setObject(blocks[0]));
                }
                // Warm both pipelines and full-sized recording pools before timing.
                {
                    auto encoder = queue->createCommandEncoder();
                    auto pass = encoder->beginComputePass();
                    for (uint32_t i = 0; i < dispatchCount; ++i)
                    {
                        pass->bindPipeline(shaders[i % 2].pipeline, roots[i % 2]);
                        pass->dispatchCompute(1, 1, 1);
                    }
                    pass->end();
                    queue->submit(encoder->finish());
                    queue->waitOnHost();
                }

                ParameterBlockCacheStats stats;
                ScopedParameterBlockCacheStats capture(stats);
                ArenaAllocator::AllocationStats allocations;
                CaptureAllocations captureAllocations(allocations);
                ProcessMemoryUsage before, retained;
                getProcessMemoryUsage(&before);
                auto start = Clock::now();
                auto encoder = queue->createCommandEncoder();
                for (uint32_t i = 0; i < dispatchCount; ++i)
                {
                    uint32_t index = alternating ? i % 2 : 0;
                    REQUIRE_CALL(ShaderCursor(roots[index])["iteration"].setData(i));
                    REQUIRE_CALL(ShaderCursor(roots[index])["scene"].setObject(blocks[i % distinctCount]));
                    auto pass = encoder->beginComputePass();
                    pass->bindPipeline(shaders[index].pipeline, roots[index]);
                    pass->dispatchCompute(1, 1, 1);
                    pass->end();
                }
                auto recorded = Clock::now();
                auto command = encoder->finish();
                REQUIRE(command);
                auto finished = Clock::now();
                getProcessMemoryUsage(&retained);
                queue->submit(command);
                auto submitted = Clock::now();
                queue->waitOnHost();
                auto completed = Clock::now();
                std::fprintf(
                    stderr,
                    "\nPB-cache %s blocks=%u programs=%u: record=%.3f finish=%.3f submit=%.3f wait=%.3f ms; "
                    "prepare=%zu hit=%zu uniform-bytes=%zu upload-bytes=%zu arena-allocs=%zu arena-bytes=%zu "
                    "new-page-bytes=%zu "
                    "retained-process-bytes=%lld\n",
                    finalized ? "finalized" : "mutable",
                    distinctCount,
                    alternating ? 2 : 1,
                    milliseconds(start, recorded),
                    milliseconds(recorded, finished),
                    milliseconds(finished, submitted),
                    milliseconds(submitted, completed),
                    stats.preparations,
                    stats.hits,
                    stats.ordinaryDataBytes,
                    stats.uploadBytesPrepared,
                    allocations.count,
                    allocations.bytes,
                    allocations.pageBytes,
                    (long long)retained.commitOrVirtualBytes - (long long)before.commitOrVirtualBytes
                );
                compareComputeResult(device, output, makeArray<float>(6.f));
            }
        }
    }
}
