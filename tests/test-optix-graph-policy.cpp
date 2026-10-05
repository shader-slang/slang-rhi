#include "test-ray-tracing-common.h"

using namespace rhi;
using namespace rhi::testing;

GPU_TEST_CASE("optix-graph-policy", CUDA)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");

    auto queue = device->getQueue(QueueType::Graphics);
    SingleTriangleBLAS blas(device, queue);
    const float transform[] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 2};
    TLAS singleLevel(device, queue, blas.blas, transform);
    TLAS nested(device, queue, singleLevel.tlas, transform);
    ResultBuffer resultBuffer(device, sizeof(float) * 2);

    using Flags = OptixTraversableGraphFlags;
    struct GraphCase
    {
        const char* name;
        bool useExtension;
        Flags flags;
        uint32_t depth;
        IAccelerationStructure* scene;
        float distance;
    };
    const GraphCase cases[] = {
        {"default", false, Flags::AllowSingleLevelInstancing, 0, singleLevel.tlas, 3},
        {"single GAS auto", true, Flags::AllowSingleGAS, 0, blas.blas, 1},
        {"single GAS explicit", true, Flags::AllowSingleGAS, 1, blas.blas, 1},
        {"single level auto", true, Flags::AllowSingleLevelInstancing, 0, singleLevel.tlas, 3},
        {"single level explicit", true, Flags::AllowSingleLevelInstancing, 2, singleLevel.tlas, 3},
        {"union GAS", true, Flags::AllowSingleGAS | Flags::AllowSingleLevelInstancing, 0, blas.blas, 1},
        {"union IAS", true, Flags::AllowSingleGAS | Flags::AllowSingleLevelInstancing, 2, singleLevel.tlas, 3},
        {"general GAS", true, Flags::AllowAny, 1, blas.blas, 1},
        {"general IAS", true, Flags::AllowAny, 2, singleLevel.tlas, 3},
        {"general nested auto", true, Flags::AllowAny, 0, nested.tlas, 5},
        {"general nested explicit", true, Flags::AllowAny, 3, nested.tlas, 5},
    };
    for (auto policy : {PipelineCompilationPolicy::Immediate, PipelineCompilationPolicy::Deferred})
    {
        CAPTURE(policy);
        for (const auto& test : cases)
        {
            CAPTURE(test.name);
            OptixRayTracingPipelineDesc options;
            options.traversableGraphFlags = test.flags;
            options.maxTraversableGraphDepth = test.depth;
            RayTracingTestPipeline pipeline(
                device,
                "test-optix-graph-policy",
                {"raygenGraphPolicy"},
                {{"closestHitGraphPolicy"}},
                {"missGraphPolicy"},
                RayTracingPipelineFlags::None,
                nullptr,
                {},
                test.useExtension ? &options : nullptr,
                policy
            );
            if (test.useExtension)
            {
                // Check ownership before dispatch: a borrowed descriptor must fail here instead
                // of launching a nested graph with an incompatible single-level contract.
                options.traversableGraphFlags = Flags::AllowSingleLevelInstancing;
                options.maxTraversableGraphDepth = 1;
                const auto* retained =
                    static_cast<const OptixRayTracingPipelineDesc*>(pipeline.raytracingPipeline->getDesc().next);
                REQUIRE(retained != nullptr);
                REQUIRE(retained != &options);
                REQUIRE(retained->traversableGraphFlags == test.flags);
                REQUIRE(retained->maxTraversableGraphDepth == test.depth);
            }
            launchPipeline(
                queue,
                pipeline.raytracingPipeline,
                pipeline.shaderTable,
                resultBuffer.resultBuffer,
                test.scene
            );
            ComPtr<ISlangBlob> result;
            resultBuffer.getFromDevice(result.writeRef());
            const auto* values = static_cast<const float*>(result->getBufferPointer());
            CHECK(values[0] == test.distance);
            CHECK(values[1] == -1.0f);
        }
    }
}

GPU_TEST_CASE("optix-graph-policy-invalid", CUDA)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");

    ComPtr<IShaderProgram> program;
    REQUIRE_CALL(loadProgram(
        device,
        "test-optix-graph-policy",
        {"raygenGraphPolicy", "closestHitGraphPolicy", "missGraphPolicy"},
        program.writeRef()
    ));
    HitGroupDesc hitGroup;
    hitGroup.hitGroupName = "hit";
    hitGroup.closestHitEntryPoint = "closestHitGraphPolicy";
    OptixRayTracingPipelineDesc options;
    RayTracingPipelineDesc desc;
    desc.next = &options;
    desc.program = program;
    desc.hitGroupCount = 1;
    desc.hitGroups = &hitGroup;
    desc.maxRayPayloadSize = 4;
    desc.maxAttributeSizeInBytes = 8;
    desc.maxRecursion = 1;
    desc.compilationPolicy = PipelineCompilationPolicy::Immediate;

    using Flags = OptixTraversableGraphFlags;
    struct InvalidCase
    {
        Flags flags;
        uint32_t depth;
    };
    const InvalidCase cases[] = {
        {static_cast<Flags>(4), 0},
        {Flags::AllowSingleLevelInstancing, 1},
        {Flags::AllowSingleGAS | Flags::AllowSingleLevelInstancing, 1},
        {Flags::AllowAny, UINT32_MAX},
        {Flags::AllowSingleGAS, UINT32_MAX},
    };
    for (const auto& test : cases)
    {
        CAPTURE(test.flags);
        CAPTURE(test.depth);
        options.traversableGraphFlags = test.flags;
        options.maxTraversableGraphDepth = test.depth;
        ComPtr<IRayTracingPipeline> pipeline;
        CHECK(device->createRayTracingPipeline(desc, pipeline.writeRef()) == SLANG_E_INVALID_ARG);
        CHECK(pipeline == nullptr);
    }
}
