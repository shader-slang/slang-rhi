#include "testing.h"
#include "stress/stress-context.h"
#include "stress/stress-shaders.h"

#include <sstream>
#include <vector>

using namespace rhi;
using namespace rhi::testing;
using namespace rhi::testing::stress;

namespace {

static constexpr uint32_t kEntryCount = 256;

struct ResourceLifetimeStress
{
    StressContext stress;
    ComPtr<IComputePipeline> pipeline;
    ComPtr<IBuffer> accumBuffer;
    std::vector<uint32_t> expected;

    ResourceLifetimeStress(GpuTestContext* ctx, IDevice* device)
        : stress(ctx, device, "stress-resource-lifetime")
        , expected(kEntryCount, 0)
    {
    }

    void createPipeline()
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
        REQUIRE_CALL(stress.device()->createComputePipeline(pipelineDesc, pipeline.writeRef()));
        stress.stats().pipelinesCreated++;
    }

    void createAccumBuffer()
    {
        const uint64_t bufferBytes = kEntryCount * sizeof(uint32_t);
        REQUIRE(stress.reserveBudget(bufferBytes));

        BufferDesc desc = {};
        desc.size = bufferBytes;
        desc.elementSize = sizeof(uint32_t);
        desc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess | BufferUsage::CopySource |
                     BufferUsage::CopyDestination;
        desc.defaultState = ResourceState::UnorderedAccess;
        desc.memoryType = MemoryType::DeviceLocal;

        std::vector<uint32_t> zero(kEntryCount, 0);
        REQUIRE_CALL(stress.device()->createBuffer(desc, zero.data(), accumBuffer.writeRef()));
        stress.stats().buffersCreated++;
    }

    void runBufferOperation(uint64_t iteration)
    {
        const uint64_t bufferBytes = kEntryCount * sizeof(uint32_t);
        if (!stress.reserveBudget(bufferBytes))
        {
            stress.wait();
            validate();
            REQUIRE(stress.reserveBudget(bufferBytes));
        }

        uint32_t value = uint32_t((iteration * 1664525ull + stress.rng().nextU32()) & 0x7fffffffu);

        BufferDesc desc = {};
        desc.size = bufferBytes;
        desc.elementSize = sizeof(uint32_t);
        desc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess;
        desc.defaultState = ResourceState::UnorderedAccess;
        desc.memoryType = MemoryType::DeviceLocal;

        ComPtr<IBuffer> tempBuffer;
        REQUIRE_CALL(stress.device()->createBuffer(desc, nullptr, tempBuffer.writeRef()));
        stress.stats().buffersCreated++;

        auto encoder = stress.queue()->createCommandEncoder();
        auto pass = encoder->beginComputePass();
        auto rootObject = pass->bindPipeline(pipeline);
        ShaderCursor cursor(rootObject->getEntryPoint(0));
        cursor["accum"].setBinding(accumBuffer);
        cursor["temp"].setBinding(tempBuffer);
        cursor["value"].setData(value);
        cursor["count"].setData(kEntryCount);
        pass->dispatchCompute((kEntryCount + 63) / 64, 1, 1);
        pass->end();

        std::ostringstream op;
        op << "compute-temp-buffer value=" << value;
        stress.recordOperation(op.str());
        stress.submit(encoder->finish());

        for (uint32_t i = 0; i < kEntryCount; ++i)
            expected[i] += value + i;

        tempBuffer = nullptr;
        stress.releaseBudget(bufferBytes);
    }

    void runTextureOperation(uint64_t iteration)
    {
        uint32_t width = 8 + stress.rng().nextRange(24);
        uint32_t height = 8 + stress.rng().nextRange(24);
        uint64_t textureBytes = uint64_t(width) * uint64_t(height) * sizeof(uint32_t);
        if (!stress.reserveBudget(textureBytes))
            return;

        TextureDesc textureDesc = {};
        textureDesc.type = TextureType::Texture2D;
        textureDesc.size = {width, height, 1};
        textureDesc.format = Format::R32Uint;
        textureDesc.usage = TextureUsage::UnorderedAccess | TextureUsage::CopySource | TextureUsage::CopyDestination;
        textureDesc.defaultState = ResourceState::UnorderedAccess;
        textureDesc.memoryType = MemoryType::DeviceLocal;

        ComPtr<ITexture> texture;
        REQUIRE_CALL(stress.device()->createTexture(textureDesc, nullptr, texture.writeRef()));
        stress.stats().texturesCreated++;

        TextureViewDesc viewDesc = {};
        viewDesc.format = Format::R32Uint;
        ComPtr<ITextureView> view;
        REQUIRE_CALL(stress.device()->createTextureView(texture, viewDesc, view.writeRef()));
        stress.stats().viewsCreated++;

        SamplerDesc samplerDesc = {};
        ComPtr<ISampler> sampler;
        REQUIRE_CALL(stress.device()->createSampler(samplerDesc, sampler.writeRef()));
        stress.stats().samplersCreated++;

        uint32_t clearValue[4] = {uint32_t(iteration), 0, 0, 0};
        auto encoder = stress.queue()->createCommandEncoder();
        encoder->clearTextureUint(texture, kEntireTexture, clearValue);

        std::ostringstream op;
        op << "clear-temp-texture " << width << "x" << height << " value=" << clearValue[0];
        stress.recordOperation(op.str());
        stress.submit(encoder->finish());

        sampler = nullptr;
        view = nullptr;
        texture = nullptr;
        stress.releaseBudget(textureBytes);
    }

    void validate()
    {
        stress.stats().validations++;
        std::vector<uint32_t> result(kEntryCount, 0);
        REQUIRE_CALL(stress.device()->readBuffer(accumBuffer, 0, kEntryCount * sizeof(uint32_t), result.data()));
        for (uint32_t i = 0; i < kEntryCount; ++i)
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
        createPipeline();
        createAccumBuffer();

        while (stress.shouldContinue())
        {
            uint64_t iteration = stress.beginIteration();
            if (stress.rng().chance(1, 4))
                runTextureOperation(iteration);
            else
                runBufferOperation(iteration);

            if (stress.shouldValidate())
            {
                stress.wait();
                validate();
            }
            stress.reportProgressIfDue();
        }

        stress.finalWait();
        validate();
        accumBuffer = nullptr;
        pipeline = nullptr;
        stress.flushReleasedResources();
        stress.reportFinal();
    }
};

} // namespace

GPU_STRESS_TEST_CASE("stress-resource-lifetime", D3D11 | D3D12 | Vulkan | Metal | CUDA | DontCacheDevice)
{
    ResourceLifetimeStress(ctx, device).run();
}
