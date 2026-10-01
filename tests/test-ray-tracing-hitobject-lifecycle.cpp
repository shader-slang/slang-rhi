#include "testing.h"
#include "test-ray-tracing-common.h"

#include <array>

using namespace rhi;
using namespace rhi::testing;

namespace {

constexpr float kForward[2][12] = {
    {2, 0, 0, 1, 0, 4, 0, 2, 0, 0, 2, 3},
    {4, 0, 0, 8, 0, 2, 0, -2, 0, 0, 4, 1},
};
// Invert each authored diagonal scale and subtract the correspondingly scaled translation.
constexpr float kInverse[2][12] = {
    {0.5f, 0, 0, -0.5f, 0, 0.25f, 0, -0.5f, 0, 0, 0.5f, -1.5f},
    {0.25f, 0, 0, -2, 0, 0.5f, 0, 1, 0, 0, 0.25f, -0.25f},
};

uint32_t floatBits(float value)
{
    uint32_t result;
    memcpy(&result, &value, sizeof(result));
    return result;
}

} // namespace

GPU_TEST_CASE("ray-tracing-hitobject-lifecycle", CUDA | DontCreateDevice)
{
    for (auto optimization : {SLANG_OPTIMIZATION_LEVEL_NONE, SLANG_OPTIMIZATION_LEVEL_MAXIMAL})
    {
        CAPTURE(optimization);
        DeviceExtraOptions options = {};
        options.compilerOptions.push_back(slang::CompilerOptionEntry{
            slang::CompilerOptionName::Optimization,
            {slang::CompilerOptionValueKind::Int, static_cast<int32_t>(optimization)},
        });
        auto device = createTestingDevice(ctx, ctx->deviceType, false, &options);
        REQUIRE(device != nullptr);
        if (!device->hasFeature(Feature::RayTracing))
            SKIP("ray tracing not supported");
        auto queue = device->getQueue(QueueType::Graphics);
        SingleTriangleBLAS blas(device, queue, true);
        std::vector<AccelerationStructureInstanceDescGeneric> instances(2);
        for (uint32_t i = 0; i < 2; ++i)
        {
            auto& instance = instances[i];
            for (uint32_t r = 0; r < 3; ++r)
                for (uint32_t c = 0; c < 4; ++c)
                    instance.transform[r][c] = kForward[i][r * 4 + c];
            instance.instanceID = i ? 0x1234 : 0xf00d;
            instance.instanceMask = 0xff;
            instance.instanceContributionToHitGroupIndex = 0;
            instance.accelerationStructure = blas.blas->getHandle();
        }
        TLAS tlas(device, queue, instances);
        RayTracingTestPipeline pipeline(
            device,
            "test-ray-tracing-hitobject-lifecycle",
            {"lifecycleRayGen"},
            {{"lifecycleClosestHit0", "lifecycleAnyHit0"}, {"lifecycleClosestHit1", "lifecycleAnyHit1"}},
            {"lifecycleMiss0", "lifecycleMiss1"}
        );

        std::array<uint32_t, 533> initial;
        initial.fill(0xa5a5a5a5);
        initial.front() = 0x13579bdf;
        initial.back() = 0x2468ace0;
        BufferDesc desc = {};
        desc.size = sizeof(initial);
        desc.elementSize = sizeof(uint32_t);
        desc.usage = BufferUsage::UnorderedAccess | BufferUsage::CopySource;
        desc.defaultState = ResourceState::UnorderedAccess;
        auto output = device->createBuffer(desc, initial.data());
        REQUIRE(output != nullptr);
        auto encoder = queue->createCommandEncoder();
        auto pass = encoder->beginRayTracingPass();
        auto rootObject = pass->bindPipeline(pipeline.raytracingPipeline, pipeline.shaderTable);
        ShaderCursor cursor(rootObject);
        cursor["sceneBVH"].setBinding(tlas.tlas);
        cursor["lifecycleResults"].setBinding(output);
        pass->dispatchRays(0, 1, 1, 1);
        pass->end();
        REQUIRE_CALL(queue->submit(encoder->finish()));
        REQUIRE_CALL(queue->waitOnHost());
        ComPtr<ISlangBlob> blob;
        REQUIRE_CALL(device->readBuffer(output, 0, sizeof(initial), blob.writeRef()));
        REQUIRE_EQ(blob->getBufferSize(), sizeof(initial));
        const auto* actual = static_cast<const uint32_t*>(blob->getBufferPointer());

        std::array<uint32_t, 533> expected = {};
        expected.front() = initial.front();
        expected.back() = initial.back();
        for (uint32_t row = 0; row < 10; ++row)
        {
            uint32_t kind = row % 4;
            uint32_t* e = expected.data() + 1 + row * 50;
            e[0] = kind < 2 ? 1 : kind == 2 ? 2 : 3;
            if (kind == 3)
                continue;
            e[6] = row >= 4 && kind != 1 ? 1 : 0;
            const float origins[3][3] = {{1.5f, 2.5f, 3}, {9, -1.75f, 1}, {30, 30, -1}};
            for (uint32_t j = 0; j < 3; ++j)
                e[9 + j] = floatBits(origins[kind][j]);
            e[14] = floatBits(kind == 1 ? 4 : 2);
            e[15] = floatBits(0.25f);
            e[16] = floatBits(kind == 2 ? 4 : 1);
            if (kind == 2)
                continue;
            e[1] = kind;
            e[2] = kind ? 0x1234 : 0xf00d;
            e[5] = 255; // +Z rays see the back of the counterclockwise triangle.
            e[17] = floatBits(0.25f);
            e[18] = floatBits(0.125f);
            e[22] = floatBits(1);
            e[23] = floatBits(0.25f);
            e[24] = floatBits(0.125f);
            for (uint32_t j = 0; j < 12; ++j)
            {
                e[25 + j] = floatBits(kForward[kind][j]);
                e[37 + j] = floatBits(kInverse[kind][j]);
            }
        }
        // Invocation uses one evolving payload: +100,+100,+200,+100,+400,+400,+0.
        const uint32_t invocations[21] = {
            150, 0xf00d, 1,      250, 0x1234, 2,      450, 0xf00d, 3,      550, 0x1234,
            4,   950,    0xbeef, 6,   1350,   0xbeef, 8,   1350,   0xbeef, 8,
        };
        memcpy(expected.data() + 501, invocations, sizeof(invocations));
        const uint32_t traversal[9] = {1000, 64, 1, 1001, 64, 1, 7, 0, 0};
        memcpy(expected.data() + 522, traversal, sizeof(traversal));
        expected[531] = 0xc001c0de;
        for (uint32_t word = 0; word < expected.size(); ++word)
        {
            CAPTURE(word);
            if (word >= 1 && word <= 500 && (word - 1) % 50 >= 25 && (word - 1) % 50 <= 48)
            {
                float a, e;
                memcpy(&a, &actual[word], sizeof(a));
                memcpy(&e, &expected[word], sizeof(e));
                // Binary-exact coefficients need no tolerance; +0 and -0 are equivalent.
                CHECK_EQ(a, e);
            }
            else
            {
                CHECK_EQ(actual[word], expected[word]);
            }
        }
    }
}
