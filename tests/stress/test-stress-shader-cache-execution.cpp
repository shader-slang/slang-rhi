#include "testing.h"
#include "stress/stress-cache.h"
#include "stress/stress-context.h"
#include "stress/stress-shaders.h"

#include <algorithm>
#include <sstream>
#include <vector>

using namespace rhi;
using namespace rhi::testing;
using namespace rhi::testing::stress;

namespace {

struct ShaderCacheExecutionStress
{
    GpuTestContext* ctx = nullptr;
    InstrumentedCache cache;
    ComPtr<IDevice> device;
    StressContext* stress = nullptr;

    explicit ShaderCacheExecutionStress(GpuTestContext* ctx_)
        : ctx(ctx_)
    {
    }

    void createDevice()
    {
        DeviceExtraOptions extraOptions = {};
        extraOptions.persistentPipelineCache = &cache;
        extraOptions.enableCompilationReports = true;
        device = createTestingDevice(ctx, ctx->deviceType, false, &extraOptions);
        REQUIRE(device != nullptr);
    }

    uint32_t expectedValue(uint32_t variant, uint32_t baseValue, uint32_t index)
    {
        uint32_t x = baseValue + index + variant;
        return (x ^ (0x9e3779b9u + variant * 17u)) + (x << 6) + (x >> 2);
    }

    void runVariant(uint64_t iteration)
    {
        uint32_t corpus = std::max(1u, stress->options().shaderCorpus);
        uint32_t variant = uint32_t((iteration / 2) % corpus);
        uint32_t baseValue = uint32_t(iteration * 13 + 7);

        ComPtr<IShaderProgram> shaderProgram;
        REQUIRE_CALL(loadComputeProgramFromSource(device, makeVariantComputeShader(variant), shaderProgram.writeRef()));
        stress->stats().shaderProgramsCreated++;

        ComputePipelineDesc pipelineDesc = {};
        pipelineDesc.program = shaderProgram.get();
        ComPtr<IComputePipeline> pipeline;
        REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));
        stress->stats().pipelinesCreated++;

        uint32_t initialData[4] = {};
        BufferDesc bufferDesc = {};
        bufferDesc.size = sizeof(initialData);
        bufferDesc.elementSize = sizeof(uint32_t);
        bufferDesc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess | BufferUsage::CopySource;
        bufferDesc.defaultState = ResourceState::UnorderedAccess;
        ComPtr<IBuffer> buffer;
        REQUIRE_CALL(device->createBuffer(bufferDesc, initialData, buffer.writeRef()));
        stress->stats().buffersCreated++;

        auto encoder = stress->queue()->createCommandEncoder();
        auto pass = encoder->beginComputePass();
        auto rootObject = pass->bindPipeline(pipeline);
        ShaderCursor cursor(rootObject->getEntryPoint(0));
        cursor["buffer"].setBinding(buffer);
        cursor["baseValue"].setData(baseValue);
        pass->dispatchCompute(1, 1, 1);
        pass->end();

        std::ostringstream op;
        op << "shader-variant variant=" << variant << " base=" << baseValue;
        stress->recordOperation(op.str());
        stress->submit(encoder->finish());
        stress->wait();

        uint32_t result[4] = {};
        REQUIRE_CALL(device->readBuffer(buffer, 0, sizeof(result), result));
        for (uint32_t i = 0; i < 4; ++i)
        {
            uint32_t expected = expectedValue(variant, baseValue, i);
            if (result[i] != expected)
            {
                CAPTURE(variant);
                CAPTURE(baseValue);
                CAPTURE(i);
                stress->captureState();
                CHECK_EQ(result[i], expected);
                return;
            }
        }
        stress->stats().validations++;

        cache.evictTo(std::max<size_t>(1, stress->options().shaderCorpus));
    }

    void reportCacheStats()
    {
        MESSAGE(
            "stress shader cache stats: queries="
            << cache.stats.queryCount << " hits=" << cache.stats.hitCount << " misses=" << cache.stats.missCount
            << " writes=" << cache.stats.writeCount << " entries=" << cache.stats.entryCount
            << " evictions=" << cache.stats.evictCount
        );
    }

    void run()
    {
        createDevice();
        StressContext localStress(ctx, device, "stress-shader-cache-execution");
        stress = &localStress;

        while (stress->shouldContinue())
        {
            uint64_t iteration = stress->beginIteration();
            runVariant(iteration);
            stress->reportProgressIfDue();
        }

        stress->finalWait();
        reportCacheStats();
        if (device->hasFeature(Feature::PipelineCache) && stress->stats().iterations > 1)
        {
            CHECK_GT(cache.stats.queryCount, 0);
            CHECK_GT(cache.stats.writeCount, 0);
        }
        stress->flushReleasedResources();
        stress->reportFinal();
        device = nullptr;
    }
};

} // namespace

GPU_STRESS_TEST_CASE("stress-shader-cache-execution", D3D12 | Vulkan | CUDA | DontCreateDevice)
{
    ShaderCacheExecutionStress(ctx).run();
}
