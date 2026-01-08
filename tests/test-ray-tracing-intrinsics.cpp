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
    float triangleVertices[9]; // 3 vertices × 3 components
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

constexpr std::array<float, 12> kWorldToObjectTransform = {
    1.0f, 0.0f, 0.0f, -1.0f,
    0.0f, 1.0f, 0.0f, -2.0f,
    0.0f, 0.0f, 1.0f, -3.0f,
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
        bool applyInstanceTransform = false
    )
    {
        ComPtr<ICommandQueue> queue = device->getQueue(QueueType::Graphics);

        const bool enableAnyHit = anyHitName != nullptr;
        SingleTriangleBLAS blas(device, queue, enableAnyHit);

        TLAS tlas = TLAS(device, queue, blas.blas, applyInstanceTransform ? kInstanceTransform.data() : nullptr);

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

GPU_TEST_CASE("ray-tracing-intrinsics-object-ray-origin", ALL)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");

    constexpr std::array<float, 3> kExpectedObjectRayOrigin =
        applyPointTransform(kWorldToObjectTransform, kRayOriginWorld);

    RayTracingTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(RayIntrinsicResult));

    // OptiX only allows calling ObjectRayOrigin from any hit or intersection.
    const char* closestHitName = "closestHitWriteObjectRayOrigin";
    const char* anyHitName = nullptr;
    if (device->getInfo().deviceType == DeviceType::CUDA)
    {
        closestHitName = nullptr;
        anyHitName = "anyHitWriteObjectRayOrigin";
    }

    test.run("rayGenShaderObjectRayOrigin", closestHitName, anyHitName, "missNOP", /*applyInstanceTransform=*/true);

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    const auto* result = reinterpret_cast<const RayIntrinsicResult*>(resultBlob->getBufferPointer());

    checkFloat3(result->value, kExpectedObjectRayOrigin);
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
        /*applyInstanceTransform=*/true
    );

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    const auto* result = reinterpret_cast<const RayIntrinsicResult*>(resultBlob->getBufferPointer());

    checkFloat3(result->value, kRayOriginWorld);
}

GPU_TEST_CASE("ray-tracing-intrinsics-object-ray-direction", ALL)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");

    constexpr std::array<float, 3> kExpectedObjectRayDirection =
        applyVectorTransform(kWorldToObjectTransform, kWorldRayDirection);

    RayTracingTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(RayIntrinsicResult));

    // OptiX only allows calling ObjectRayDirection from any hit or intersection.
    const char* closestHitName = "closestHitWriteObjectRayDirection";
    const char* anyHitName = nullptr;
    if (device->getInfo().deviceType == DeviceType::CUDA)
    {
        closestHitName = nullptr;
        anyHitName = "anyHitWriteObjectRayDirection";
    }

    test.run("rayGenShaderObjectRayOrigin", closestHitName, anyHitName, "missNOP", /*applyInstanceTransform=*/true);

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    const auto* result = reinterpret_cast<const RayIntrinsicResult*>(resultBlob->getBufferPointer());

    checkFloat3(result->value, kExpectedObjectRayDirection);
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
        /*applyInstanceTransform=*/true
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

// Callable shaders haven't been implemented for the CUDA/OptiX backend in Slang
GPU_TEST_CASE("ray-tracing-intrinsics-call-shader", D3D12 | Vulkan)
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
