#include "testing.h"
#include "stress/stress-cache.h"
#include "stress/stress-context.h"
#include "stress/stress-shaders.h"

#include <sstream>
#include <vector>

using namespace rhi;
using namespace rhi::testing;
using namespace rhi::testing::stress;

namespace {

static constexpr uint32_t kMixedEntryCount = 128;

struct MixedStress
{
    StressContext stress;
    InstrumentedCache cache;
    ComPtr<IComputePipeline> lifetimePipeline;
    ComPtr<IBuffer> accumBuffer;
    std::vector<uint32_t> expected;

    MixedStress(GpuTestContext* ctx, IDevice* device)
        : stress(ctx, device, "stress-mixed")
        , expected(kMixedEntryCount, 0)
    {
    }

    void createLifetimePipeline()
    {
        ComPtr<IShaderProgram> shaderProgram;
        REQUIRE_CALL(loadComputeProgramFromSource(
            stress.device(),
            makeLifetimeCanaryComputeShader(),
            shaderProgram.writeRef()
        ));
        stress.stats().shaderProgramsCreated++;

        ComputePipelineDesc pipelineDesc = {};
        pipelineDesc.program = shaderProgram.get();
        REQUIRE_CALL(stress.device()->createComputePipeline(pipelineDesc, lifetimePipeline.writeRef()));
        stress.stats().pipelinesCreated++;
    }

    void createAccumBuffer()
    {
        BufferDesc desc = {};
        desc.size = kMixedEntryCount * sizeof(uint32_t);
        desc.elementSize = sizeof(uint32_t);
        desc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess | BufferUsage::CopySource |
                     BufferUsage::CopyDestination;
        desc.defaultState = ResourceState::UnorderedAccess;
        desc.memoryType = MemoryType::DeviceLocal;
        std::vector<uint32_t> zero(kMixedEntryCount, 0);
        REQUIRE_CALL(stress.device()->createBuffer(desc, zero.data(), accumBuffer.writeRef()));
        stress.stats().buffersCreated++;
    }

    void runLifetimeOp(uint64_t iteration)
    {
        uint32_t value = uint32_t((iteration * 1103515245ull + 12345u) & 0xffffu);

        BufferDesc desc = {};
        desc.size = kMixedEntryCount * sizeof(uint32_t);
        desc.elementSize = sizeof(uint32_t);
        desc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess;
        desc.defaultState = ResourceState::UnorderedAccess;
        desc.memoryType = MemoryType::DeviceLocal;
        ComPtr<IBuffer> tempBuffer;
        REQUIRE_CALL(stress.device()->createBuffer(desc, nullptr, tempBuffer.writeRef()));
        stress.stats().buffersCreated++;

        auto encoder = stress.queue()->createCommandEncoder();
        auto pass = encoder->beginComputePass();
        auto rootObject = pass->bindPipeline(lifetimePipeline);
        ShaderCursor cursor(rootObject->getEntryPoint(0));
        cursor["accum"].setBinding(accumBuffer);
        cursor["temp"].setBinding(tempBuffer);
        cursor["value"].setData(value);
        cursor["count"].setData(kMixedEntryCount);
        pass->dispatchCompute((kMixedEntryCount + 63) / 64, 1, 1);
        pass->end();

        std::ostringstream op;
        op << "mixed-lifetime value=" << value;
        stress.recordOperation(op.str());
        stress.submit(encoder->finish());

        for (uint32_t i = 0; i < kMixedEntryCount; ++i)
            expected[i] += value + i;
    }

    void runShaderOp(uint64_t iteration)
    {
        uint32_t variant = uint32_t((iteration / 2) % std::max(1u, stress.options().shaderCorpus));
        uint32_t baseValue = uint32_t(iteration * 3 + 1);

        ComPtr<IShaderProgram> shaderProgram;
        REQUIRE_CALL(loadComputeProgramFromSource(stress.device(), makeVariantComputeShader(variant), shaderProgram.writeRef()));
        stress.stats().shaderProgramsCreated++;

        ComputePipelineDesc pipelineDesc = {};
        pipelineDesc.program = shaderProgram.get();
        ComPtr<IComputePipeline> pipeline;
        REQUIRE_CALL(stress.device()->createComputePipeline(pipelineDesc, pipeline.writeRef()));
        stress.stats().pipelinesCreated++;

        uint32_t data[4] = {};
        BufferDesc desc = {};
        desc.size = sizeof(data);
        desc.elementSize = sizeof(uint32_t);
        desc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess | BufferUsage::CopySource;
        desc.defaultState = ResourceState::UnorderedAccess;
        ComPtr<IBuffer> buffer;
        REQUIRE_CALL(stress.device()->createBuffer(desc, data, buffer.writeRef()));
        stress.stats().buffersCreated++;

        auto encoder = stress.queue()->createCommandEncoder();
        auto pass = encoder->beginComputePass();
        auto rootObject = pass->bindPipeline(pipeline);
        ShaderCursor cursor(rootObject->getEntryPoint(0));
        cursor["buffer"].setBinding(buffer);
        cursor["baseValue"].setData(baseValue);
        pass->dispatchCompute(1, 1, 1);
        pass->end();

        std::ostringstream op;
        op << "mixed-shader variant=" << variant;
        stress.recordOperation(op.str());
        stress.submit(encoder->finish());
        stress.wait();

        uint32_t result[4] = {};
        REQUIRE_CALL(stress.device()->readBuffer(buffer, 0, sizeof(result), result));
        uint32_t x = baseValue + variant;
        uint32_t expected0 = (x ^ (0x9e3779b9u + variant * 17u)) + (x << 6) + (x >> 2);
        if (result[0] != expected0)
        {
            stress.captureState();
            CHECK_EQ(result[0], expected0);
        }
        stress.stats().validations++;
    }

    void runTextureOp(uint64_t iteration)
    {
        TextureDesc desc = {};
        desc.type = TextureType::Texture2D;
        desc.size = {16, 16, 1};
        desc.format = Format::R32Uint;
        desc.usage = TextureUsage::UnorderedAccess | TextureUsage::CopySource;
        desc.defaultState = ResourceState::UnorderedAccess;
        ComPtr<ITexture> texture;
        REQUIRE_CALL(stress.device()->createTexture(desc, nullptr, texture.writeRef()));
        stress.stats().texturesCreated++;

        uint32_t clearValue[4] = {uint32_t(iteration), 0, 0, 0};
        auto encoder = stress.queue()->createCommandEncoder();
        encoder->clearTextureUint(texture, kEntireTexture, clearValue);
        stress.recordOperation("mixed-clear-texture");
        stress.submit(encoder->finish());
    }

    void validateLifetime()
    {
        stress.stats().validations++;
        stress.wait();
        std::vector<uint32_t> result(kMixedEntryCount, 0);
        REQUIRE_CALL(stress.device()->readBuffer(accumBuffer, 0, kMixedEntryCount * sizeof(uint32_t), result.data()));
        for (uint32_t i = 0; i < kMixedEntryCount; ++i)
        {
            if (result[i] != expected[i])
            {
                CAPTURE(i);
                stress.captureState();
                CHECK_EQ(result[i], expected[i]);
                return;
            }
        }
    }

    void run()
    {
        createLifetimePipeline();
        createAccumBuffer();

        while (stress.shouldContinue())
        {
            uint64_t iteration = stress.beginIteration();
            uint32_t choice = stress.rng().nextRange(100);
            if (choice < 45)
                runLifetimeOp(iteration);
            else if (choice < 70)
                runTextureOp(iteration);
            else
                runShaderOp(iteration);

            if (stress.shouldValidate())
                validateLifetime();
            stress.reportProgressIfDue();
        }

        stress.finalWait();
        validateLifetime();
        accumBuffer = nullptr;
        lifetimePipeline = nullptr;
        stress.flushReleasedResources();
        stress.reportFinal();
    }
};

} // namespace

GPU_STRESS_TEST_CASE("stress-mixed", D3D12 | Vulkan | CUDA | DontCacheDevice)
{
    MixedStress(ctx, device).run();
}
