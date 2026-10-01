#include "testing.h"
#include "test-ray-tracing-common.h"

#include <array>

using namespace rhi;
using namespace rhi::testing;

GPU_TEST_CASE("ray-tracing-transform-list", CUDA | DontCreateDevice)
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
        // Observations are idempotent; duplicate AnyHit invocations do not accumulate state.
        SingleTriangleBLAS blas(device, queue, true);
        NativeHandle nativeBLAS;
        REQUIRE_CALL(blas.blas->getNativeHandle(&nativeBLAS));
        REQUIRE(nativeBLAS.type == NativeHandleType::OptixTraversableHandle);
        REQUIRE_NE(nativeBLAS.value, 0);
        std::vector<AccelerationStructureInstanceDescGeneric> instances(2);
        for (uint32_t index = 0; index < instances.size(); ++index)
        {
            auto& instance = instances[index];
            instance.transform[0][0] = 1.0f;
            instance.transform[1][1] = 1.0f;
            instance.transform[2][2] = 1.0f;
            instance.transform[0][3] = index == 0 ? 0.0f : 4.0f;
            instance.instanceID = index == 0 ? 0xF00D : 0x1234;
            instance.instanceMask = 0xff;
            instance.instanceContributionToHitGroupIndex = 0;
            instance.accelerationStructure = blas.blas->getHandle();
        }
        TLAS tlas(device, queue, instances);
        RayTracingTestPipeline pipeline(
            device,
            "test-ray-tracing-transform-list",
            {"transformListRayGen"},
            {{"transformListClosestHit", "transformListAnyHit"}},
            {"transformListMiss"}
        );

        // Two isolated hits and one miss each return 17 words, between untouched guards.
        std::array<uint32_t, 53> initial;
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
        cursor["transformListResults"].setBinding(output);
        const uint32_t listIndex = 0;
        cursor["transformListIndex"].setData(&listIndex, sizeof(listIndex));
        pass->dispatchRays(0, 3, 1, 1);
        pass->end();
        REQUIRE_CALL(queue->submit(encoder->finish()));
        REQUIRE_CALL(queue->waitOnHost());
        ComPtr<ISlangBlob> blob;
        REQUIRE_CALL(device->readBuffer(output, 0, sizeof(initial), blob.writeRef()));
        REQUIRE_EQ(blob->getBufferSize(), sizeof(initial));
        const auto* actual = static_cast<const uint32_t*>(blob->getBufferPointer());
        CHECK_EQ(actual[0], initial.front());
        CHECK_EQ(actual[52], initial.back());
        for (uint32_t ray = 0; ray < 3; ++ray)
        {
            CAPTURE(ray);
            const uint32_t base = 1 + ray * 17;
            CHECK_EQ(actual[base + 16], ray < 2 ? 3 : 4);
            if (ray == 2)
            {
                for (uint32_t word = 0; word < 16; ++word)
                {
                    CAPTURE(word);
                    CHECK_EQ(actual[base + word], 0xdead0000u + word);
                }
                continue;
            }
            for (uint32_t stage = 0; stage < 2; ++stage)
            {
                CAPTURE(stage);
                const uint32_t offset = base + stage * 8;
                CHECK_EQ(actual[offset], 1);
                CHECK_EQ(actual[offset + 1], listIndex);
                // The scene independently supplies an instance (SDK type 4) and its custom ID.
                CHECK_EQ(actual[offset + 2], 4);
                CHECK_EQ(actual[offset + 3], ray == 0 ? 0xF00D : 0x1234);
                const uint64_t handle = uint64_t(actual[offset + 4]) | (uint64_t(actual[offset + 5]) << 32);
                const uint64_t child = uint64_t(actual[offset + 6]) | (uint64_t(actual[offset + 7]) << 32);
                CHECK_NE(handle, 0);
                CHECK_EQ(child, nativeBLAS.value);
            }
            // The two stages visit the same instance; its opaque handle has no fixed portable value.
            CHECK_EQ(actual[base + 4], actual[base + 12]);
            CHECK_EQ(actual[base + 5], actual[base + 13]);
        }
    }
}
