#include "testing.h"
#include "stress/stress-context.h"
#include "test-ray-tracing-common.h"

#include <vector>

using namespace rhi;
using namespace rhi::testing;
using namespace rhi::testing::stress;

namespace {

struct AccelerationStructureLifetimeStress
{
    StressContext stress;
    ComPtr<ITexture> resultTexture;
    ComPtr<IRayTracingPipeline> pipeline;
    ComPtr<IShaderTable> shaderTable;
    uint32_t width = 16;
    uint32_t height = 16;

    AccelerationStructureLifetimeStress(GpuTestContext* ctx, IDevice* device)
        : stress(ctx, device, "stress-acceleration-structure-lifetime")
    {
    }

    void createPipeline()
    {
        std::vector<const char*> raygenNames = {"rayGenShaderIdx0"};
        std::vector<HitGroupProgramNames> hitGroupProgramNames = {
            {"closestHitShaderIdx0", /*anyhit=*/nullptr, /*intersection=*/nullptr},
        };
        std::vector<const char*> missNames = {"missShaderIdx0"};

        RayTracingTestPipeline rtPipeline(
            stress.device(),
            "test-ray-tracing",
            raygenNames,
            hitGroupProgramNames,
            missNames
        );
        pipeline = rtPipeline.raytracingPipeline;
        shaderTable = rtPipeline.shaderTable;
        stress.stats().shaderProgramsCreated++;
        stress.stats().pipelinesCreated++;
    }

    void createResultTexture()
    {
        TextureDesc desc = {};
        desc.type = TextureType::Texture2D;
        desc.size = {width, height, 1};
        desc.mipCount = 1;
        desc.format = Format::RGBA32Float;
        desc.usage = TextureUsage::UnorderedAccess | TextureUsage::CopySource;
        desc.defaultState = ResourceState::UnorderedAccess;
        REQUIRE_CALL(stress.device()->createTexture(desc, nullptr, resultTexture.writeRef()));
        stress.stats().texturesCreated++;
    }

    void trace(IAccelerationStructure* tlas)
    {
        auto encoder = stress.queue()->createCommandEncoder();
        auto pass = encoder->beginRayTracingPass();
        auto rootObject = pass->bindPipeline(pipeline, shaderTable);
        ShaderCursor cursor(rootObject);
        uint32_t dims[2] = {width, height};
        cursor["dims"].setData(dims, sizeof(dims));
        cursor["resultTexture"].setBinding(resultTexture);
        cursor["sceneBVH"].setBinding(tlas);
        pass->dispatchRays(0, width, height, 1);
        pass->end();
        stress.recordOperation("trace-fresh-tlas");
        stress.submit(encoder->finish());
        stress.wait();
    }

    void validate()
    {
        stress.stats().validations++;
        ComPtr<ISlangBlob> resultBlob;
        SubresourceLayout layout;
        REQUIRE_CALL(stress.device()->readTexture(resultTexture, 0, 0, resultBlob.writeRef(), &layout));
        auto pixel = [&](uint32_t x, uint32_t y) -> const float*
        {
            return reinterpret_cast<const float*>(
                static_cast<const uint8_t*>(resultBlob->getBufferPointer()) + y * layout.rowPitch +
                x * layout.colPitch
            );
        };

        const float* hit = pixel(width / 2, height / 2);
        const float* miss = pixel(0, 0);
        bool ok = hit[0] == 1.f && hit[1] == 0.f && hit[2] == 0.f && hit[3] == 1.f && miss[0] == 1.f &&
                  miss[1] == 1.f && miss[2] == 1.f && miss[3] == 1.f;
        if (!ok)
        {
            stress.captureState();
            CHECK_EQ(hit[0], 1.f);
            CHECK_EQ(hit[1], 0.f);
            CHECK_EQ(hit[2], 0.f);
            CHECK_EQ(hit[3], 1.f);
            CHECK_EQ(miss[0], 1.f);
            CHECK_EQ(miss[1], 1.f);
            CHECK_EQ(miss[2], 1.f);
            CHECK_EQ(miss[3], 1.f);
        }
    }

    void run()
    {
        if (!stress.options().enableRayTracing)
            SKIP("ray tracing stress requires -stress-enable-rt");
        if (!stress.device()->hasFeature(Feature::AccelerationStructure))
            SKIP("acceleration structures not supported");
        if (!stress.device()->hasFeature(Feature::RayTracing))
            SKIP("ray tracing not supported");

        createPipeline();
        createResultTexture();

        while (stress.shouldContinue())
        {
            stress.beginIteration();
            ThreeTriangleBLAS blas(stress.device(), stress.queue());
            TLAS tlas(stress.device(), stress.queue(), blas.blas);
            stress.stats().accelerationStructuresCreated += 3;
            trace(tlas.tlas);
            validate();
            stress.reportProgressIfDue();
        }

        stress.finalWait();
        resultTexture = nullptr;
        pipeline = nullptr;
        shaderTable = nullptr;
        stress.flushReleasedResources();
        stress.reportFinal();
    }
};

} // namespace

GPU_STRESS_TEST_CASE("stress-acceleration-structure-lifetime", D3D12 | Vulkan | CUDA | DontCacheDevice)
{
    AccelerationStructureLifetimeStress(ctx, device).run();
}
