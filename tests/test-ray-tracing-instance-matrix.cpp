#include "testing.h"
#include "test-ray-tracing-common.h"

#include <array>

using namespace rhi;
using namespace rhi::testing;

namespace {

// Row-major affine matrices combine unequal scale, exchanged axes, shear and translation.
constexpr float kObjectToWorld[2][12] = {
    {0.0f, -2.0f, 0.0f, 1.0f, 4.0f, 1.0f, 0.0f, 2.0f, 0.0f, 0.0f, 8.0f, 3.0f},
    {2.0f, 0.0f, 1.0f, 8.0f, 0.0f, 0.0f, -4.0f, -2.0f, 0.0f, 8.0f, 2.0f, 1.0f},
};

// Solve the two affine systems explicitly. Every coefficient is an exact binary fraction.
// First: y=(1-X)/2, x=(Y-2-y)/4, z=(Z-3)/8.
// Second: z=-(Y+2)/4, y=(Z-1-2*z)/8, x=(X-8-z)/2.
constexpr float kWorldToObject[2][12] = {
    {0.125f, 0.25f, 0.0f, -0.625f, -0.5f, 0.0f, 0.0f, 0.5f, 0.0f, 0.0f, 0.125f, -0.375f},
    {0.5f, 0.125f, 0.0f, -3.75f, 0.0f, 0.0625f, 0.125f, 0.0f, 0.0f, -0.25f, 0.0f, -0.5f},
};

} // namespace

GPU_TEST_CASE("ray-tracing-instance-matrix", CUDA | DontCreateDevice)
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
        SingleTriangleBLAS blas(device, queue);
        std::vector<AccelerationStructureInstanceDescGeneric> instances(2);
        for (uint32_t index = 0; index < instances.size(); ++index)
        {
            auto& instance = instances[index];
            for (uint32_t row = 0; row < 3; ++row)
                for (uint32_t column = 0; column < 4; ++column)
                    instance.transform[row][column] = kObjectToWorld[index][row * 4 + column];
            instance.instanceID = index == 0 ? 0xF00D : 0x1234;
            instance.instanceMask = 0xff;
            instance.instanceContributionToHitGroupIndex = 0;
            instance.accelerationStructure = blas.blas->getHandle();
        }
        TLAS tlas(device, queue, instances);
        RayTracingTestPipeline pipeline(
            device,
            "test-ray-tracing-instance-matrix",
            {"instanceMatrixRayGen"},
            {{"instanceMatrixClosestHit", nullptr}},
            {"instanceMatrixMiss"}
        );

        // Two hits and one miss each return 24 coefficients plus phase, between untouched guards.
        std::array<uint32_t, 77> initial;
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
        cursor["instanceMatrixResults"].setBinding(output);
        pass->dispatchRays(0, 3, 1, 1);
        pass->end();
        REQUIRE_CALL(queue->submit(encoder->finish()));
        REQUIRE_CALL(queue->waitOnHost());
        ComPtr<ISlangBlob> blob;
        REQUIRE_CALL(device->readBuffer(output, 0, sizeof(initial), blob.writeRef()));
        REQUIRE_EQ(blob->getBufferSize(), sizeof(initial));
        const auto* actual = static_cast<const uint32_t*>(blob->getBufferPointer());
        CHECK_EQ(actual[0], initial.front());
        CHECK_EQ(actual[76], initial.back());
        for (uint32_t ray = 0; ray < 3; ++ray)
        {
            CAPTURE(ray);
            const uint32_t base = 1 + ray * 25;
            CHECK_EQ(actual[base + 24], ray < 2 ? 1 : 2);
            for (uint32_t word = 0; word < 24; ++word)
            {
                CAPTURE(word);
                float coefficient;
                memcpy(&coefficient, &actual[base + word], sizeof(coefficient));
                const float expected = ray == 2    ? -float(128 + word)
                                       : word < 12 ? kObjectToWorld[ray][word]
                                                   : kWorldToObject[ray][word - 12];
                // Exact numeric equality; the sign of a zero matrix coefficient is not part of this oracle.
                CHECK_EQ(coefficient, expected);
            }
        }
    }
}
