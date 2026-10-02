#include "testing.h"
#include "test-ray-tracing-common.h"

#include <array>

using namespace rhi;
using namespace rhi::testing;

namespace {

struct RayIntrinsicResult
{
    float value[3];
    int32_t isHit;
    uint32_t hitKind;
    float rayTMin;
    float rayTCurrent;
    uint32_t rayFlags;
    uint32_t geometryIndex;
    float triangleVertices[9]; // 3 vertices x 3 components
    float rayCurrentTime;
    uint32_t instanceID;
    uint32_t instanceIndex;
};

// clang-format off
constexpr std::array<float, 12> kInstanceTransform = {
    1.0f, 0.0f, 0.0f,  1.0f,
    0.0f, 1.0f, 0.0f,  2.0f,
    0.0f, 0.0f, 1.0f,  3.0f,
};

// Swapped, scaled axes distinguish object direction from world direction and normalization.
constexpr std::array<float, 12> kObjectInstanceTransform = {
    0.0f, -2.0f, 0.0f, 1.0f,
    4.0f,  0.0f, 0.0f, 2.0f,
    0.0f,  0.0f, 8.0f, 3.0f,
};

constexpr std::array<float, 12> kObjectWorldToObjectTransform = {
     0.0f, 0.25f, 0.0f,   -0.5f,
    -0.5f, 0.0f,  0.0f,    0.5f,
     0.0f, 0.0f,  0.125f, -0.375f,
};
// clang-format on

constexpr std::array<float, 3> applyPointTransform(const std::array<float, 12>& matrix, const std::array<float, 3>& p)
{
    return {
        matrix[0] * p[0] + matrix[1] * p[1] + matrix[2] * p[2] + matrix[3],
        matrix[4] * p[0] + matrix[5] * p[1] + matrix[6] * p[2] + matrix[7],
        matrix[8] * p[0] + matrix[9] * p[1] + matrix[10] * p[2] + matrix[11],
    };
}

constexpr std::array<float, 3> applyVectorTransform(const std::array<float, 12>& matrix, const std::array<float, 3>& v)
{
    return {
        matrix[0] * v[0] + matrix[1] * v[1] + matrix[2] * v[2],
        matrix[4] * v[0] + matrix[5] * v[1] + matrix[6] * v[2],
        matrix[8] * v[0] + matrix[9] * v[1] + matrix[10] * v[2],
    };
}

constexpr std::array<float, 3> subtract(const std::array<float, 3>& a, const std::array<float, 3>& b)
{
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}

constexpr std::array<float, 3> kTrianglePointObject = {0.25f, 0.25f, 1.0f};
constexpr std::array<float, 3> kRayOriginWorld = {0.0f, 0.0f, 0.0f};

constexpr std::array<float, 3> kTrianglePointWorld = applyPointTransform(kInstanceTransform, kTrianglePointObject);
constexpr std::array<float, 3> kWorldRayDirection = subtract(kTrianglePointWorld, kRayOriginWorld);
constexpr std::array<float, 3> kObjectTestTargetWorld =
    applyPointTransform(kObjectInstanceTransform, kTrianglePointObject);
constexpr std::array<float, 3> kObjectTestDirectionWorld = subtract(kObjectTestTargetWorld, kRayOriginWorld);

void checkFloat3(const float* actual, const std::array<float, 3>& expected)
{
    CHECK_EQ(actual[0], expected[0]);
    CHECK_EQ(actual[1], expected[1]);
    CHECK_EQ(actual[2], expected[2]);
}

struct RayTracingTriangleTest
{
    IDevice* device = nullptr;

    void init(IDevice* device_) { device = device_; }

    ResultBuffer resultBuf;

    void createResultBuffer(size_t resultSize) { resultBuf = ResultBuffer(device, resultSize); }

    void run(
        const char* raygenName,
        const char* closestHitName,
        const char* anyHitName = nullptr,
        const char* missName = "missNOP",
        const float* instanceTransform = nullptr
    )
    {
        ComPtr<ICommandQueue> queue = device->getQueue(QueueType::Graphics);

        const bool enableAnyHit = anyHitName != nullptr;
        SingleTriangleBLAS blas(device, queue, enableAnyHit);

        TLAS tlas = TLAS(device, queue, blas.blas, instanceTransform);

        std::vector<HitGroupProgramNames> hitGroupProgramNames = {{closestHitName, anyHitName}};
        std::vector<const char*> missNames = {missName};

        RayTracingTestPipeline
            pipeline(device, "test-ray-tracing-intrinsics", {raygenName}, hitGroupProgramNames, missNames);

        launchPipeline(queue, pipeline.raytracingPipeline, pipeline.shaderTable, resultBuf.resultBuffer, tlas.tlas);
    }

    ComPtr<ISlangBlob> getTestResult()
    {
        ComPtr<ISlangBlob> resultBlob;
        resultBuf.getFromDevice(resultBlob.writeRef());
        return resultBlob;
    }
};

struct RayTracingMotionBlurTriangleTest
{
    IDevice* device = nullptr;

    void init(IDevice* device_) { device = device_; }

    ResultBuffer resultBuf;

    void createResultBuffer(size_t resultSize) { resultBuf = ResultBuffer(device, resultSize); }

    void run(const char* raygenName, const char* closestHitName, const char* missName = "missNOPAttribute")
    {
        ComPtr<ICommandQueue> queue = device->getQueue(QueueType::Graphics);

        SingleTriangleVertexMotionBLAS blas(device, queue);
        VertexMotionInstanceTLAS tlas(device, queue, blas.blas, 2);

        std::vector<HitGroupProgramNames> hitGroupProgramNames = {{closestHitName, nullptr}};
        std::vector<const char*> missNames = {missName};

        RayTracingTestPipeline pipeline(
            device,
            "test-ray-tracing-intrinsics",
            {raygenName},
            hitGroupProgramNames,
            missNames,
            RayTracingPipelineFlags::EnableMotion
        );

        launchPipeline(queue, pipeline.raytracingPipeline, pipeline.shaderTable, resultBuf.resultBuffer, tlas.tlas);
    }

    ComPtr<ISlangBlob> getTestResult()
    {
        ComPtr<ISlangBlob> resultBlob;
        resultBuf.getFromDevice(resultBlob.writeRef());
        return resultBlob;
    }
};

} // namespace

GPU_TEST_CASE("ray-tracing-intrinsics-object-ray-origin", ALL | DontCreateDevice)
{
    for (auto optimization : {SLANG_OPTIMIZATION_LEVEL_NONE, SLANG_OPTIMIZATION_LEVEL_MAXIMAL})
    {
        CAPTURE(optimization);
        DeviceExtraOptions options = {};
        options.compilerOptions.push_back(slang::CompilerOptionEntry{
            slang::CompilerOptionName::Optimization,
            {slang::CompilerOptionValueKind::Int, static_cast<int32_t>(optimization)},
        });
        auto testDevice = createTestingDevice(ctx, ctx->deviceType, false, &options);
        REQUIRE(testDevice != nullptr);
        if (!testDevice->hasFeature(Feature::RayTracing))
            SKIP("ray tracing not supported");

        constexpr std::array<float, 3> kExpectedObjectRayOrigin =
            applyPointTransform(kObjectWorldToObjectTransform, kRayOriginWorld);

        RayTracingTriangleTest test;
        test.init(testDevice);
        test.createResultBuffer(sizeof(RayIntrinsicResult));

        // OptiX only allows calling ObjectRayOrigin from any hit or intersection.
        const char* closestHitName = "closestHitWriteObjectRayOrigin";
        const char* anyHitName = nullptr;
        if (testDevice->getInfo().deviceType == DeviceType::CUDA)
        {
            closestHitName = nullptr;
            anyHitName = "anyHitWriteObjectRayOrigin";
        }

        test.run("rayGenShaderObjectRayOrigin", closestHitName, anyHitName, "missNOP", kObjectInstanceTransform.data());

        ComPtr<ISlangBlob> resultBlob = test.getTestResult();
        const auto* result = reinterpret_cast<const RayIntrinsicResult*>(resultBlob->getBufferPointer());

        checkFloat3(result->value, kExpectedObjectRayOrigin);
    }
}

GPU_TEST_CASE("ray-tracing-intrinsics-world-ray-origin", ALL)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");

    RayTracingTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(RayIntrinsicResult));
    test.run(
        "rayGenShaderWorldRayOrigin",
        "closestHitWriteWorldRayOrigin",
        nullptr,
        "missNOP",
        kInstanceTransform.data()
    );

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    const auto* result = reinterpret_cast<const RayIntrinsicResult*>(resultBlob->getBufferPointer());

    checkFloat3(result->value, kRayOriginWorld);
}

GPU_TEST_CASE("ray-tracing-intrinsics-object-ray-direction", ALL | DontCreateDevice)
{
    for (auto optimization : {SLANG_OPTIMIZATION_LEVEL_NONE, SLANG_OPTIMIZATION_LEVEL_MAXIMAL})
    {
        CAPTURE(optimization);
        DeviceExtraOptions options = {};
        options.compilerOptions.push_back(slang::CompilerOptionEntry{
            slang::CompilerOptionName::Optimization,
            {slang::CompilerOptionValueKind::Int, static_cast<int32_t>(optimization)},
        });
        auto testDevice = createTestingDevice(ctx, ctx->deviceType, false, &options);
        REQUIRE(testDevice != nullptr);
        if (!testDevice->hasFeature(Feature::RayTracing))
            SKIP("ray tracing not supported");

        constexpr std::array<float, 3> kExpectedObjectRayDirection =
            applyVectorTransform(kObjectWorldToObjectTransform, kObjectTestDirectionWorld);

        RayTracingTriangleTest test;
        test.init(testDevice);
        test.createResultBuffer(sizeof(RayIntrinsicResult));

        // OptiX only allows calling ObjectRayDirection from any hit or intersection.
        const char* closestHitName = "closestHitWriteObjectRayDirection";
        const char* anyHitName = nullptr;
        if (testDevice->getInfo().deviceType == DeviceType::CUDA)
        {
            closestHitName = nullptr;
            anyHitName = "anyHitWriteObjectRayDirection";
        }

        test.run(
            "rayGenShaderObjectRayDirection",
            closestHitName,
            anyHitName,
            "missNOP",
            kObjectInstanceTransform.data()
        );

        ComPtr<ISlangBlob> resultBlob = test.getTestResult();
        const auto* result = reinterpret_cast<const RayIntrinsicResult*>(resultBlob->getBufferPointer());

        checkFloat3(result->value, kExpectedObjectRayDirection);
    }
}

GPU_TEST_CASE("ray-tracing-intrinsics-world-ray-direction", ALL)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");

    RayTracingTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(RayIntrinsicResult));
    test.run(
        "rayGenShaderWorldRayDirection",
        "closestHitWriteWorldRayDirection",
        nullptr,
        "missNOP",
        kInstanceTransform.data()
    );

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    const auto* result = reinterpret_cast<const RayIntrinsicResult*>(resultBlob->getBufferPointer());

    checkFloat3(result->value, kWorldRayDirection);
}

GPU_TEST_CASE("ray-tracing-intrinsics-accept-hit-and-end-search", ALL)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");

    RayTracingTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(RayIntrinsicResult));

    // The anyhit shader calls AcceptHitAndEndSearch, so closesthit should be invoked
    test.run("rayGenShaderAnyhitTest", "closestHitSetHit", "anyhitAcceptAndEnd");

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    const auto* result = reinterpret_cast<const RayIntrinsicResult*>(resultBlob->getBufferPointer());

    // Verify closesthit was invoked - isHit should be 1
    CHECK_EQ(result->isHit, 1);
}

GPU_TEST_CASE("ray-tracing-intrinsics-ignore-hit", ALL)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");

    RayTracingTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(RayIntrinsicResult));

    // The anyhit shader calls IgnoreHit, so we should miss
    test.run("rayGenShaderAnyhitTest", "closestHitSetHit", "anyhitIgnore");

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    const auto* result = reinterpret_cast<const RayIntrinsicResult*>(resultBlob->getBufferPointer());

    // Verify we missed - isHit should be 0
    CHECK_EQ(result->isHit, 0);
}

GPU_TEST_CASE("ray-tracing-intrinsics-hit-kind", ALL)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");

    RayTracingTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(RayIntrinsicResult));
    test.run("rayGenShaderAttributeTest", "closestHitWriteHitKind");

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    const auto* result = reinterpret_cast<const RayIntrinsicResult*>(resultBlob->getBufferPointer());

    // HIT_KIND_TRIANGLE_BACK_FACE = 0xFF
    CHECK_EQ(result->hitKind, 0xFF);
}

GPU_TEST_CASE("ray-tracing-intrinsics-ray-tmin", ALL)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");

    RayTracingTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(RayIntrinsicResult));
    test.run("rayGenShaderAttributeTest", "closestHitWriteRayTMin");

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    const auto* result = reinterpret_cast<const RayIntrinsicResult*>(resultBlob->getBufferPointer());

    // Should match the TMin value set in the ray (0.001)
    CHECK_EQ(result->rayTMin, 0.001f);
}

GPU_TEST_CASE("ray-tracing-intrinsics-ray-tcurrent", ALL)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");

    RayTracingTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(RayIntrinsicResult));
    test.run("rayGenShaderAttributeTest", "closestHitWriteRayTCurrent");

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    const auto* result = reinterpret_cast<const RayIntrinsicResult*>(resultBlob->getBufferPointer());

    // Should be greater than TMin and less than TMax
    CHECK(result->rayTCurrent > 0.001f);
    CHECK(result->rayTCurrent < 10000.0f);
}

GPU_TEST_CASE("ray-tracing-intrinsics-ray-flags", ALL)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");

    RayTracingTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(RayIntrinsicResult));
    test.run("rayGenShaderAttributeTest", "closestHitWriteRayFlags");

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    const auto* result = reinterpret_cast<const RayIntrinsicResult*>(resultBlob->getBufferPointer());

    // RAY_FLAG_FORCE_OPAQUE = 0x01
    CHECK_EQ(result->rayFlags, 0x01);
}

// OptiX doesn't support geometry index
GPU_TEST_CASE("ray-tracing-intrinsics-geometry-index", ALL & ~CUDA)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");

    RayTracingTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(RayIntrinsicResult));
    test.run("rayGenShaderAttributeTest", "closestHitWriteGeometryIndex");

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    const auto* result = reinterpret_cast<const RayIntrinsicResult*>(resultBlob->getBufferPointer());

    // Single geometry BLAS, so geometry index should be 0
    CHECK_EQ(result->geometryIndex, 0);
}

// Only supported for glsl and spirv backends
GPU_TEST_CASE("ray-tracing-intrinsics-hit-triangle-vertex-position", Vulkan)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");

    RayTracingTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(RayIntrinsicResult));
    test.run("rayGenShaderAttributeTest", "closestHitWriteHitTriangleVertexPosition");

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    const auto* result = reinterpret_cast<const RayIntrinsicResult*>(resultBlob->getBufferPointer());

    // Verify all 3 vertices match SingleTriangleBLAS vertices
    CHECK_EQ(result->triangleVertices[0], 0.0f);
    CHECK_EQ(result->triangleVertices[1], 0.0f);
    CHECK_EQ(result->triangleVertices[2], 1.0f);

    CHECK_EQ(result->triangleVertices[3], 1.0f);
    CHECK_EQ(result->triangleVertices[4], 0.0f);
    CHECK_EQ(result->triangleVertices[5], 1.0f);

    CHECK_EQ(result->triangleVertices[6], 0.0f);
    CHECK_EQ(result->triangleVertices[7], 1.0f);
    CHECK_EQ(result->triangleVertices[8], 1.0f);
}

GPU_TEST_CASE("ray-tracing-intrinsics-ray-current-time", ALL)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::RayTracingMotionBlur))
        SKIP("ray tracing motion blur not supported");

    RayTracingMotionBlurTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(RayIntrinsicResult));
    test.run("rayGenShaderMotionBlurAttributeTest", "closestHitWriteRayCurrentTime");

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    const auto* result = reinterpret_cast<const RayIntrinsicResult*>(resultBlob->getBufferPointer());

    // Motion blur enabled with currentTime = 0.5, should return that value
    CHECK_EQ(result->rayCurrentTime, 0.5f);
}

GPU_TEST_CASE("ray-tracing-intrinsics-instance-id", ALL)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");

    RayTracingTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(RayIntrinsicResult));
    test.run("rayGenShaderAttributeTest", "closestHitWriteInstanceID");

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    const auto* result = reinterpret_cast<const RayIntrinsicResult*>(resultBlob->getBufferPointer());

    // Instance ID is set to 0xF00D in TLAS
    CHECK_EQ(result->instanceID, 0xF00D);
}

GPU_TEST_CASE("ray-tracing-intrinsics-instance-index", ALL)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");

    RayTracingTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(RayIntrinsicResult));
    test.run("rayGenShaderAttributeTest", "closestHitWriteInstanceIndex");

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    const auto* result = reinterpret_cast<const RayIntrinsicResult*>(resultBlob->getBufferPointer());

    // Single instance in TLAS, so instance index should be 0
    CHECK_EQ(result->instanceIndex, 0);
}

GPU_TEST_CASE("ray-tracing-intrinsics-call-shader", D3D12 | Vulkan | CUDA)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");

    ComPtr<ICommandQueue> queue = device->getQueue(QueueType::Graphics);

    // Create a simple BLAS (not actually used, but needed for pipeline creation)
    SingleTriangleBLAS blas(device, queue, false);

    // Create TLAS
    TLAS tlas(device, queue, blas.blas);

    // Create result buffer
    ResultBuffer resultBuf(device, sizeof(RayIntrinsicResult));

    // Set up pipeline with callable shader
    std::vector<const char*> raygenNames = {"rayGenShaderCallShaderTest"};
    std::vector<HitGroupProgramNames> hitGroupProgramNames = {{"closestHitNOP", nullptr}};
    std::vector<const char*> missNames = {"missNOP"};
    std::vector<const char*> callableNames = {"callableWriteAttribute"};

    RayTracingTestPipeline pipeline(
        device,
        "test-ray-tracing-intrinsics",
        raygenNames,
        hitGroupProgramNames,
        missNames,
        RayTracingPipelineFlags::None,
        nullptr,
        callableNames
    );

    // Launch pipeline
    launchPipeline(queue, pipeline.raytracingPipeline, pipeline.shaderTable, resultBuf.resultBuffer, tlas.tlas);

    // Verify results
    ComPtr<ISlangBlob> resultBlob;
    resultBuf.getFromDevice(resultBlob.writeRef());
    const auto* result = reinterpret_cast<const RayIntrinsicResult*>(resultBlob->getBufferPointer());

    // Check that callable shader wrote the expected value
    checkFloat3(result->value, {1.0f, 2.0f, 3.0f});
}

GPU_TEST_CASE("ray-tracing-intrinsics-hit-identities", ALL | DontCreateDevice)
{
    for (auto optimization : {SLANG_OPTIMIZATION_LEVEL_NONE, SLANG_OPTIMIZATION_LEVEL_MAXIMAL})
    {
        CAPTURE(optimization);
        DeviceExtraOptions options = {};
        options.compilerOptions.push_back(slang::CompilerOptionEntry{
            slang::CompilerOptionName::Optimization,
            {slang::CompilerOptionValueKind::Int, static_cast<int32_t>(optimization)},
        });
        auto testDevice = createTestingDevice(ctx, ctx->deviceType, false, &options);
        REQUIRE(testDevice != nullptr);
        if (!testDevice->hasFeature(Feature::RayTracing))
            SKIP("ray tracing not supported");

        auto queue = testDevice->getQueue(QueueType::Graphics);
        ThreeTriangleBLAS blas(testDevice, queue);
        // Translation separates the instances; distinct custom IDs cannot be confused with their ordinal indices.
        std::vector<AccelerationStructureInstanceDescGeneric> instances(2);
        for (uint32_t index = 0; index < instances.size(); ++index)
        {
            auto& instance = instances[index];
            instance.transform[0][0] = 1.0f;
            instance.transform[1][1] = 1.0f;
            instance.transform[2][2] = 1.0f;
            instance.transform[0][3] = index == 0 ? 0.0f : 4.0f;
            instance.instanceID = index == 0 ? 0xF00D : 0x1234;
            instance.instanceMask = 0xFF;
            instance.instanceContributionToHitGroupIndex = 0;
            instance.accelerationStructure = blas.blas->getHandle();
        }
        TLAS tlas(testDevice, queue, instances);
        RayTracingTestPipeline pipeline(
            testDevice,
            "test-ray-tracing-intrinsics",
            {"rayGenShaderIdentities"},
            {{"closestHitWriteIdentities", nullptr}},
            {"missWriteIdentities"}
        );

        // Initialize every output word so missing writes and either guard overwrite fail.
        std::array<uint32_t, 44> initial;
        initial.fill(0xa5a5a5a5);
        initial.front() = 0x13579bdf;
        initial.back() = 0x2468ace0;
        BufferDesc desc = {};
        desc.size = sizeof(initial);
        desc.elementSize = sizeof(uint32_t);
        desc.usage = BufferUsage::UnorderedAccess | BufferUsage::CopySource;
        desc.defaultState = ResourceState::UnorderedAccess;
        auto output = testDevice->createBuffer(desc, initial.data());
        REQUIRE(output != nullptr);

        auto encoder = queue->createCommandEncoder();
        auto pass = encoder->beginRayTracingPass();
        auto rootObject = pass->bindPipeline(pipeline.raytracingPipeline, pipeline.shaderTable);
        ShaderCursor cursor(rootObject);
        cursor["sceneBVH"].setBinding(tlas.tlas);
        cursor["identityResults"].setBinding(output);
        pass->dispatchRays(0, 7, 1, 1);
        pass->end();
        REQUIRE_CALL(queue->submit(encoder->finish()));
        REQUIRE_CALL(queue->waitOnHost());

        ComPtr<ISlangBlob> blob;
        REQUIRE_CALL(testDevice->readBuffer(output, 0, sizeof(initial), blob.writeRef()));
        REQUIRE_EQ(blob->getBufferSize(), sizeof(initial));
        const auto* actual = static_cast<const uint32_t*>(blob->getBufferPointer());
        CHECK_EQ(actual[0], initial.front());
        CHECK_EQ(actual[43], initial.back());
        for (uint32_t index = 0; index < 7; ++index)
        {
            CAPTURE(index);
            const uint32_t offset = 1 + index * 6;
            CHECK_EQ(actual[offset], index < 6 ? index % 3 : 0xdead0001);
            CHECK_EQ(actual[offset + 1], index < 6 ? index / 3 : 0xdead0002);
            CHECK_EQ(actual[offset + 2], index < 6 ? (index < 3 ? 0xF00D : 0x1234) : 0xdead0003);
            CHECK_EQ(actual[offset + 3], index < 6 ? 1 : 0);
            CHECK_EQ(actual[offset + 4], index % 2 == 0 ? 1 : 0);
            // For +Z rays, triangles 0/1 are back-facing and triangle 2 is front-facing.
            CHECK_EQ(actual[offset + 5], index < 6 ? (index % 3 == 2 ? 254 : 255) : 0xdead0004);
        }
    }
}

GPU_TEST_CASE("ray-tracing-intrinsics-payload-termination", CUDA | Vulkan | D3D12 | DontCreateDevice)
{
    // Expectations come from the writes before termination, including nested inout copyback.
    constexpr std::array<uint32_t, 9> expectedValues = {11, 12, 21, 21, 91, 31, 31, 93, 42};
    constexpr std::array<uint32_t, 9> expectedPhases = {1, 2, 1, 2, 1, 1, 2, 1, 1};
    for (auto optimization : {SLANG_OPTIMIZATION_LEVEL_NONE, SLANG_OPTIMIZATION_LEVEL_MAXIMAL})
    {
        CAPTURE(optimization);
        DeviceExtraOptions options = {};
        options.compilerOptions.push_back(slang::CompilerOptionEntry{
            slang::CompilerOptionName::Optimization,
            {slang::CompilerOptionValueKind::Int, static_cast<int32_t>(optimization)},
        });
        auto testDevice = createTestingDevice(ctx, ctx->deviceType, false, &options);
        REQUIRE(testDevice != nullptr);
        if (!testDevice->hasFeature(Feature::RayTracing))
            SKIP("ray tracing not supported");

        auto queue = testDevice->getQueue(QueueType::Graphics);
        SingleTriangleBLAS blas(testDevice, queue, true);
        TLAS tlas(testDevice, queue, blas.blas);
        RayTracingTestPipeline pipeline(
            testDevice,
            "test-ray-tracing-intrinsics",
            {"rayGenPayloadTermination"},
            {{"closestHitPayloadTermination", "anyHitPayloadTermination"}},
            {"missPayloadTermination"}
        );

        // Change only the uniform mode between launches of the same compiled pipeline.
        for (uint32_t mode = 0; mode < expectedValues.size(); ++mode)
        {
            CAPTURE(mode);
            const std::array<uint32_t, 5> initial = {0x13579bdf, 0xa5a5a5a5, 0xa5a5a5a5, 0xa5a5a5a5, 0x2468ace0};
            BufferDesc desc = {};
            desc.size = sizeof(initial);
            desc.elementSize = sizeof(uint32_t);
            desc.usage = BufferUsage::UnorderedAccess | BufferUsage::CopySource;
            desc.defaultState = ResourceState::UnorderedAccess;
            auto output = testDevice->createBuffer(desc, initial.data());
            REQUIRE(output != nullptr);

            auto encoder = queue->createCommandEncoder();
            auto pass = encoder->beginRayTracingPass();
            auto rootObject = pass->bindPipeline(pipeline.raytracingPipeline, pipeline.shaderTable);
            ShaderCursor cursor(rootObject);
            cursor["sceneBVH"].setBinding(tlas.tlas);
            cursor["terminationMode"].setData(&mode, sizeof(mode));
            cursor["terminationResults"].setBinding(output);
            pass->dispatchRays(0, 1, 1, 1);
            pass->end();
            REQUIRE_CALL(queue->submit(encoder->finish()));
            REQUIRE_CALL(queue->waitOnHost());

            ComPtr<ISlangBlob> blob;
            REQUIRE_CALL(testDevice->readBuffer(output, 0, sizeof(initial), blob.writeRef()));
            REQUIRE_EQ(blob->getBufferSize(), sizeof(initial));
            const auto* actual = static_cast<const uint32_t*>(blob->getBufferPointer());
            CHECK_EQ(actual[0], initial.front());
            CHECK_EQ(actual[1], expectedValues[mode]);
            CHECK_EQ(actual[2], 64 + mode);
            CHECK_EQ(actual[3], expectedPhases[mode]);
            CHECK_EQ(actual[4], initial.back());
        }
    }
}

GPU_TEST_CASE("ray-tracing-intrinsics-anyhit-state", CUDA | Vulkan | D3D12 | DontCreateDevice)
{
    // The three triangles use the same unequal vertex weights (u=1/4, v=1/8).
    constexpr std::array<std::array<float, 3>, 3> hitPoints = {{
        {0.25f, 0.125f, 1.0f},
        {-0.125f, 0.25f, 1.0f},
        {0.25f, -0.125f, 1.0f},
    }};
    // A power-of-two dominant component keeps the intersection in this exact fixture domain.
    constexpr std::array<float, 3> direction = {0.5f, -0.25f, 2.0f};
    for (auto optimization : {SLANG_OPTIMIZATION_LEVEL_NONE, SLANG_OPTIMIZATION_LEVEL_MAXIMAL})
    {
        CAPTURE(optimization);
        DeviceExtraOptions options = {};
        options.compilerOptions.push_back(slang::CompilerOptionEntry{
            slang::CompilerOptionName::Optimization,
            {slang::CompilerOptionValueKind::Int, static_cast<int32_t>(optimization)},
        });
        auto testDevice = createTestingDevice(ctx, ctx->deviceType, false, &options);
        REQUIRE(testDevice != nullptr);
        if (!testDevice->hasFeature(Feature::RayTracing))
            SKIP("ray tracing not supported");

        auto queue = testDevice->getQueue(QueueType::Graphics);
        ThreeTriangleBLAS blas(testDevice, queue);
        std::vector<AccelerationStructureInstanceDescGeneric> instances(2);
        for (uint32_t index = 0; index < instances.size(); ++index)
        {
            auto& instance = instances[index];
            instance.transform[0][0] = 1.0f;
            instance.transform[1][1] = 1.0f;
            instance.transform[2][2] = 1.0f;
            instance.transform[0][3] = index == 0 ? 0.0f : 4.0f;
            instance.instanceID = index == 0 ? 0xF00D : 0x1234;
            instance.instanceMask = 0xFF;
            instance.instanceContributionToHitGroupIndex = 0;
            instance.accelerationStructure = blas.blas->getHandle();
        }
        TLAS tlas(testDevice, queue, instances);
        RayTracingTestPipeline pipeline(
            testDevice,
            "test-ray-tracing-intrinsics",
            {"rayGenAnyHitState"},
            {{"closestHitStateNOP", "anyHitObserveState"}},
            {"missAnyHitState"}
        );

        std::array<uint32_t, 114> initial;
        initial.fill(0xa5a5a5a5);
        initial.front() = 0x13579bdf;
        initial.back() = 0x2468ace0;
        BufferDesc desc = {};
        desc.size = sizeof(initial);
        desc.elementSize = sizeof(uint32_t);
        desc.usage = BufferUsage::UnorderedAccess | BufferUsage::CopySource;
        desc.defaultState = ResourceState::UnorderedAccess;
        auto output = testDevice->createBuffer(desc, initial.data());
        REQUIRE(output != nullptr);

        auto encoder = queue->createCommandEncoder();
        auto pass = encoder->beginRayTracingPass();
        auto rootObject = pass->bindPipeline(pipeline.raytracingPipeline, pipeline.shaderTable);
        ShaderCursor cursor(rootObject);
        cursor["sceneBVH"].setBinding(tlas.tlas);
        cursor["anyHitStateResults"].setBinding(output);
        pass->dispatchRays(0, 7, 1, 1);
        pass->end();
        REQUIRE_CALL(queue->submit(encoder->finish()));
        REQUIRE_CALL(queue->waitOnHost());

        ComPtr<ISlangBlob> blob;
        REQUIRE_CALL(testDevice->readBuffer(output, 0, sizeof(initial), blob.writeRef()));
        REQUIRE_EQ(blob->getBufferSize(), sizeof(initial));
        const auto* actual = static_cast<const uint32_t*>(blob->getBufferPointer());
        CHECK_EQ(actual[0], initial.front());
        CHECK_EQ(actual[113], initial.back());
        for (uint32_t ray = 0; ray < 7; ++ray)
        {
            CAPTURE(ray);
            std::array<uint32_t, 16> expected;
            for (uint32_t field = 0; field < expected.size(); ++field)
                expected[field] = 0xdead0000 + field;
            if (ray < 6)
            {
                const auto& point = hitPoints[ray % 3];
                const float translation = ray < 3 ? 0.0f : 4.0f;
                const std::array<float, 8> rayValues = {
                    point[0] + translation - 2.0f * direction[0],
                    point[1] - 2.0f * direction[1],
                    point[2] - 2.0f * direction[2],
                    direction[0],
                    direction[1],
                    direction[2],
                    0.25f,
                    2.0f,
                };
                for (uint32_t field = 0; field < rayValues.size(); ++field)
                    memcpy(&expected[field], &rayValues[field], sizeof(uint32_t));
                expected[8] = ray % 2 == 0 ? 2 : 10; // FORCE_NON_OPAQUE, optionally SKIP_CLOSEST_HIT_SHADER.
                expected[9] = ray % 3;
                expected[10] = ray / 3;
                expected[11] = ray < 3 ? 0xF00D : 0x1234;
                expected[12] = ray % 3 == 2 ? 254 : 255;
                expected[13] = 0x3e800000; // Float32 1/4, weight of vertex 1.
                expected[14] = 0x3e000000; // Float32 1/8, weight of vertex 2.
                expected[15] = 1;
            }
            else
            {
                expected[15] = 2;
            }
            for (uint32_t field = 0; field < expected.size(); ++field)
            {
                CAPTURE(field);
                CHECK_EQ(actual[1 + ray * 16 + field], expected[field]);
            }
        }
    }
}

GPU_TEST_CASE("ray-tracing-intrinsics-nested-call-shader", D3D12 | Vulkan | CUDA)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");

    ComPtr<ICommandQueue> queue = device->getQueue(QueueType::Graphics);

    // The geometry is not traced, but every backend still requires a complete ray-tracing
    // pipeline and shader table for dispatch.
    SingleTriangleBLAS blas(device, queue, false);
    TLAS tlas(device, queue, blas.blas);
    ResultBuffer resultBuf(device, sizeof(RayIntrinsicResult));

    std::vector<const char*> raygenNames = {"rayGenShaderNestedCallShaderTest"};
    std::vector<HitGroupProgramNames> hitGroupProgramNames = {{"closestHitNOP", nullptr}};
    std::vector<const char*> missNames = {"missNOP"};

    // The order defines the callable shader-table indices: callableInvokeNested is entry 0 and
    // invokes callableNestedLeaf at entry 1.
    std::vector<const char*> callableNames = {"callableInvokeNested", "callableNestedLeaf"};

    OptixRayTracingPipelineDesc optixPipelineDesc = {};
    optixPipelineDesc.maxDirectCallableDepthFromState = 2;
    const void* pipelineNext =
        device->getDeviceType() == DeviceType::CUDA ? static_cast<const void*>(&optixPipelineDesc) : nullptr;

    RayTracingTestPipeline pipeline(
        device,
        "test-ray-tracing-intrinsics",
        raygenNames,
        hitGroupProgramNames,
        missNames,
        RayTracingPipelineFlags::None,
        nullptr,
        callableNames,
        8,
        pipelineNext
    );

    launchPipeline(queue, pipeline.raytracingPipeline, pipeline.shaderTable, resultBuf.resultBuffer, tlas.tlas);

    ComPtr<ISlangBlob> resultBlob;
    resultBuf.getFromDevice(resultBlob.writeRef());
    const auto* result = reinterpret_cast<const RayIntrinsicResult*>(resultBlob->getBufferPointer());

    // Outer saved value: (14, 19, 22); leaf result: (7, 13, 23); sum: (21, 32, 45).
    checkFloat3(result->value, {21.0f, 32.0f, 45.0f});
}

GPU_TEST_CASE("ray-tracing-callable-family", CUDA | DontCreateDevice)
{
    for (auto optimization : {SLANG_OPTIMIZATION_LEVEL_NONE, SLANG_OPTIMIZATION_LEVEL_MAXIMAL})
    {
        CAPTURE(optimization);
        DeviceExtraOptions options = {};
        options.compilerOptions.push_back(slang::CompilerOptionEntry{
            slang::CompilerOptionName::Optimization,
            {slang::CompilerOptionValueKind::Int, static_cast<int32_t>(optimization)},
        });
        auto testDevice = createTestingDevice(ctx, ctx->deviceType, false, &options);
        REQUIRE(testDevice != nullptr);
        if (!testDevice->hasFeature(Feature::RayTracing))
            SKIP("ray tracing not supported");
        auto queue = testDevice->getQueue(QueueType::Graphics);
        SingleTriangleBLAS blas(testDevice, queue, false);
        TLAS tlas(testDevice, queue, blas.blas);
        OptixRayTracingPipelineDesc optixDesc = {};
        optixDesc.maxDirectCallableDepthFromState = 2;
        RayTracingTestPipeline pipeline(
            testDevice,
            "test-ray-tracing-callables",
            {"raygenCallables"},
            {{"hitCallables", nullptr}},
            {"missCallables"},
            RayTracingPipelineFlags::None,
            nullptr,
            {"callableAdd", "callableChain", "callableEmpty", "callableNestedEmpty", "callableOut"},
            8,
            &optixDesc,
            PipelineCompilationPolicy::Deferred
        );
        // Compilation happens at binding, after the application changes its original options.
        // Assert ownership first so a missing copy fails without launching with an undersized stack.
        optixDesc.maxDirectCallableDepthFromState = 0;
        auto retained = static_cast<const OptixRayTracingPipelineDesc*>(pipeline.raytracingPipeline->getDesc().next);
        REQUIRE(retained != nullptr);
        REQUIRE(retained != &optixDesc);
        REQUIRE_EQ(retained->maxDirectCallableDepthFromState, 2);
        std::array<float, 458> initial;
        initial.fill(-1234.0f);
        initial.front() = 13579.0f;
        initial.back() = 24680.0f;
        BufferDesc desc = {};
        desc.size = sizeof(initial);
        desc.elementSize = sizeof(float);
        desc.usage = BufferUsage::UnorderedAccess | BufferUsage::CopySource;
        desc.defaultState = ResourceState::UnorderedAccess;
        auto output = testDevice->createBuffer(desc, initial.data());
        REQUIRE(output != nullptr);
        auto encoder = queue->createCommandEncoder();
        auto pass = encoder->beginRayTracingPass();
        auto root = pass->bindPipeline(pipeline.raytracingPipeline, pipeline.shaderTable);
        ShaderCursor cursor(root);
        cursor["sceneBVH"].setBinding(tlas.tlas);
        cursor["callableResults"].setBinding(output);
        uint32_t nestedIndex = 0;
        cursor["nestedIndex"].setData(&nestedIndex, sizeof(nestedIndex));
        pass->dispatchRays(0, 8, 1, 1);
        pass->end();
        REQUIRE_CALL(queue->submit(encoder->finish()));
        REQUIRE_CALL(queue->waitOnHost());
        ComPtr<ISlangBlob> blob;
        REQUIRE_CALL(testDevice->readBuffer(output, 0, sizeof(initial), blob.writeRef()));
        REQUIRE_EQ(blob->getBufferSize(), sizeof(initial));
        const auto* actual = static_cast<const float*>(blob->getBufferPointer());
        CHECK_EQ(actual[0], initial.front());
        CHECK_EQ(actual[457], initial.back());
        for (uint32_t lane = 0; lane < 8; ++lane)
        {
            CAPTURE(lane);
            for (uint32_t segment = 0; segment < 2; ++segment)
            {
                uint32_t stage = segment == 0 ? 0 : lane % 2 == 0 ? 1 : 2;
                uint32_t seed = stage * 100 + lane;
                uint32_t delta = lane % 2 == 0 ? 10 : 30;
                for (uint32_t leaf = 0; leaf < 27; ++leaf)
                {
                    CAPTURE(segment);
                    CAPTURE(leaf);
                    // Even lanes toggle false once; odd lanes toggle true twice.
                    float expected = leaf == 25 ? 1.0f : float(seed + leaf + delta);
                    CHECK_EQ(actual[1 + (lane * 2 + segment) * 27 + leaf], expected);
                }
            }
            CHECK_EQ(actual[433 + lane * 3], 71.0f);
            CHECK_EQ(actual[434 + lane * 3], 83.0f);
            CHECK_EQ(actual[435 + lane * 3], 97.0f);
        }
    }
}
