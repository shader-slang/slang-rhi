#include "testing.h"
#include "test-ray-tracing-common.h"

#include <array>

using namespace rhi;
using namespace rhi::testing;

namespace {

struct RayIntrinsicResult
{
    float value[3];
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

struct RayTracingSingleTriangleTest
{
    IDevice* device = nullptr;

    void init(IDevice* device_) { device = device_; }

    ResultBuffer resultBuf;

    void createResultBuffer(size_t resultSize) { resultBuf = ResultBuffer(device, resultSize); }

    void run(const char* raygenName, const char* closestHitName, const char* anyHitName = nullptr)
    {
        ComPtr<ICommandQueue> queue = device->getQueue(QueueType::Graphics);

        const bool enableAnyHit = anyHitName != nullptr;
        SingleTriangleBLAS blas(device, queue, enableAnyHit);
        TLAS tlas(device, queue, blas.blas, kInstanceTransform.data());

        std::vector<HitGroupProgramNames> hitGroupProgramNames = {{closestHitName, anyHitName}};
        std::vector<const char*> missNames = {"missNOP"};

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

} // namespace

GPU_TEST_CASE("ray-tracing-intrinsics-object-ray-origin", ALL)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");

    constexpr std::array<float, 3> kExpectedObjectRayOrigin =
        applyPointTransform(kWorldToObjectTransform, kRayOriginWorld);

    RayTracingSingleTriangleTest test;
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

    test.run("rayGenShaderObjectRayOrigin", closestHitName, anyHitName);

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    const auto* result = reinterpret_cast<const RayIntrinsicResult*>(resultBlob->getBufferPointer());

    checkFloat3(result->value, kExpectedObjectRayOrigin);
}

GPU_TEST_CASE("ray-tracing-intrinsics-world-ray-origin", ALL)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");

    RayTracingSingleTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(RayIntrinsicResult));
    test.run("rayGenShaderWorldRayOrigin", "closestHitWriteWorldRayOrigin");

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

    RayTracingSingleTriangleTest test;
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

    test.run("rayGenShaderObjectRayOrigin", closestHitName, anyHitName);

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    const auto* result = reinterpret_cast<const RayIntrinsicResult*>(resultBlob->getBufferPointer());

    checkFloat3(result->value, kExpectedObjectRayDirection);
}

GPU_TEST_CASE("ray-tracing-intrinsics-world-ray-direction", ALL)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");

    RayTracingSingleTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(RayIntrinsicResult));
    test.run("rayGenShaderWorldRayDirection", "closestHitWriteWorldRayDirection");

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    const auto* result = reinterpret_cast<const RayIntrinsicResult*>(resultBlob->getBufferPointer());

    checkFloat3(result->value, kWorldRayDirection);
}
