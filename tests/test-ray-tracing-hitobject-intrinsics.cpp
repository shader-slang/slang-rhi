#include "testing.h"
#include "test-ray-tracing-common.h"

#include <slang-rhi/acceleration-structure-utils.h>

using namespace rhi;
using namespace rhi::testing;

namespace {

struct TestResult
{
    int queryWasSuccess;
    int invokeWasSuccess;

    float rayOrigin[3];
    float rayDirection[3];
};

struct RayTracingSingleTriangleTest
{
    IDevice* device;

    void init(IDevice* device_) { this->device = device_; }

    ResultBuffer resultBuf;

    void createResultBuffer(size_t resultSize) { resultBuf = ResultBuffer(device, resultSize); }

    void run(
        const char* filepath,
        const char* raygenName,
        const std::vector<const char*>& closestHitNames,
        const std::vector<const char*>& missNames
    )
    {
        ComPtr<ICommandQueue> queue = device->getQueue(QueueType::Graphics);

        SingleTriangleBLAS blas(device, queue);
        TLAS tlas(device, queue, blas.blas);

        std::vector<HitGroupProgramNames> hitGroupProgramNames;
        for (const char* closestHitName : closestHitNames)
            hitGroupProgramNames.push_back({closestHitName, /*intersection=*/nullptr});

        RayTracingTestPipeline pipeline(device, filepath, {raygenName}, hitGroupProgramNames, missNames);
        launchPipeline(queue, pipeline.raytracingPipeline, pipeline.shaderTable, resultBuf.resultBuffer, tlas.tlas);
    }

    ComPtr<ISlangBlob> getTestResult()
    {
        ComPtr<ISlangBlob> resultBlob;
        resultBuf.getFromDevice(resultBlob.writeRef());
        return resultBlob;
    }
};

struct RayTracingSingleCustomGeometryTest
{
    IDevice* device;

    void init(IDevice* device_) { this->device = device_; }

    ResultBuffer resultBuf;

    void createResultBuffer(size_t resultSize) { resultBuf = ResultBuffer(device, resultSize); }

    void run(
        const char* filepath,
        const char* raygenName,
        const std::vector<HitGroupProgramNames>& hitGroupProgramNames,
        const std::vector<const char*>& missNames
    )
    {
        ComPtr<ICommandQueue> queue = device->getQueue(QueueType::Graphics);

        SingleCustomGeometryBLAS blas(device, queue);
        TLAS tlas(device, queue, blas.blas);

        RayTracingTestPipeline pipeline(device, filepath, {raygenName}, hitGroupProgramNames, missNames);
        launchPipeline(queue, pipeline.raytracingPipeline, pipeline.shaderTable, resultBuf.resultBuffer, tlas.tlas);

        ComPtr<ISlangBlob> resultBlob;
        resultBuf.getFromDevice(resultBlob.writeRef());
    }

    ComPtr<ISlangBlob> getTestResult()
    {
        ComPtr<ISlangBlob> resultBlob;
        resultBuf.getFromDevice(resultBlob.writeRef());
        return resultBlob;
    }
};

void checkQueryAndInvokeResult(ISlangBlob* resultBlob)
{
    const TestResult* testResult = reinterpret_cast<const TestResult*>(resultBlob->getBufferPointer());
    CHECK_EQ(testResult->queryWasSuccess, 1);
    CHECK_EQ(testResult->invokeWasSuccess, 1);
}
} // namespace

GPU_TEST_CASE("ray-tracing-hitobject-query-invoke-nop-rg", ALL)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    RayTracingSingleTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(TestResult));
    test.run("test-ray-tracing-hitobject-intrinsics", "rayGenShaderMakeQueryInvokeNOP", {"closestHitNOP"}, {"missNOP"});

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    checkQueryAndInvokeResult(resultBlob);
}

GPU_TEST_CASE("ray-tracing-hitobject-query-invoke-nop-ch", ALL)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    RayTracingSingleTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(TestResult));
    test.run(
        "test-ray-tracing-hitobject-intrinsics",
        "rayGenShaderInvokeCH",
        {"closestHitMakeQueryInvokeNOP"},
        {"missNOP"}
    );

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    checkQueryAndInvokeResult(resultBlob);
}

GPU_TEST_CASE("ray-tracing-hitobject-query-invoke-nop-ms", ALL)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    RayTracingSingleTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(TestResult));
    test.run(
        "test-ray-tracing-hitobject-intrinsics",
        "rayGenShaderInvokeMS",
        {"closestHitNOP"},
        {"missMakeQueryInvokeNOP"}
    );

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    checkQueryAndInvokeResult(resultBlob);
}

GPU_TEST_CASE("ray-tracing-hitobject-query-invoke-miss-rg", ALL)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    RayTracingSingleTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(TestResult));
    test.run(
        "test-ray-tracing-hitobject-intrinsics",
        "rayGenShaderMakeQueryInvokeMiss",
        {"closestHitNOP"},
        {"missInvoke"}
    );

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    checkQueryAndInvokeResult(resultBlob);
}

GPU_TEST_CASE("ray-tracing-hitobject-query-invoke-miss-ch", ALL)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    RayTracingSingleTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(TestResult));
    test.run(
        "test-ray-tracing-hitobject-intrinsics",
        "rayGenShaderInvokeCH",
        {"closestHitMakeQueryInvokeMiss"},
        {"missInvoke"}
    );

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    checkQueryAndInvokeResult(resultBlob);
}

GPU_TEST_CASE("ray-tracing-hitobject-query-invoke-miss-ms", ALL)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    RayTracingSingleTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(TestResult));
    test.run(
        "test-ray-tracing-hitobject-intrinsics",
        "rayGenShaderInvokeMS",
        {"closestHitNOP"},
        {"missMakeQueryInvokeMiss", "missInvoke"}
    );

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    checkQueryAndInvokeResult(resultBlob);
}

GPU_TEST_CASE("ray-tracing-hitobject-query-invoke-hit-rg", ALL)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    RayTracingSingleTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(TestResult));
    test.run(
        "test-ray-tracing-hitobject-intrinsics",
        "rayGenShaderTraceQueryInvokeHit",
        {"closestHitInvoke"},
        {"missNOP"}
    );

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    checkQueryAndInvokeResult(resultBlob);
}

GPU_TEST_CASE("ray-tracing-hitobject-query-invoke-hit-ch", ALL)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    RayTracingSingleTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(TestResult));
    test.run(
        "test-ray-tracing-hitobject-intrinsics",
        "rayGenShaderInvokeCH",
        {"closestHitMakeQueryInvokeHit", "closestHitInvoke"},
        {"missNOP"}
    );

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    checkQueryAndInvokeResult(resultBlob);
}

GPU_TEST_CASE("ray-tracing-hitobject-query-invoke-hit-ms", ALL)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    RayTracingSingleTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(TestResult));
    test.run(
        "test-ray-tracing-hitobject-intrinsics",
        "rayGenShaderInvokeMS",
        {"closestHitNOP", "closestHitInvoke"},
        {"missMakeQueryInvokeHit"}
    );

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    checkQueryAndInvokeResult(resultBlob);
}

// CUDA disabled due to https://github.com/shader-slang/slang/issues/8836
GPU_TEST_CASE("ray-tracing-hitobject-query-hit-kind-front-face", ALL & ~CUDA)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    RayTracingSingleTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(TestResult));
    test.run(
        "test-ray-tracing-hitobject-intrinsics",
        "rayGenShaderQueryHitKindFrontFace",
        {"closestHitNOP"},
        {"missNOP"}
    );

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    checkQueryAndInvokeResult(resultBlob);
}

// CUDA disabled due to https://github.com/shader-slang/slang/issues/8836
GPU_TEST_CASE("ray-tracing-hitobject-query-hit-kind-back-face", ALL & ~CUDA)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    RayTracingSingleTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(TestResult));
    test.run(
        "test-ray-tracing-hitobject-intrinsics",
        "rayGenShaderQueryHitKindBackFace",
        {"closestHitNOP"},
        {"missNOP"}
    );

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    checkQueryAndInvokeResult(resultBlob);
}

GPU_TEST_CASE("ray-tracing-hitobject-query-hit-kind-custom", ALL & ~CUDA)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    RayTracingSingleCustomGeometryTest test;
    test.init(device);
    test.createResultBuffer(sizeof(TestResult));
    test.run(
        "test-ray-tracing-hitobject-intrinsics",
        "rayGenShaderQueryHitKindCustom",
        {{"closestHitNOP", "intersectionReportHitWithKind"}},
        {"missNOP"}
    );

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    checkQueryAndInvokeResult(resultBlob);
}

// CUDA/OptiX is disabled because it only supports getting the ray origin in world space.
// D3D12 is disabled due to https://github.com/shader-slang/slang/issues/8615
GPU_TEST_CASE("ray-tracing-hitobject-query-hit-ray-object-origin", ALL & ~CUDA & ~D3D12)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    RayTracingSingleTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(TestResult));
    test.run(
        "test-ray-tracing-hitobject-intrinsics",
        "rayGenShaderQueryRayObjectOrigin",
        {"closestHitNOP"},
        {"missNOP"}
    );

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    const TestResult* result = reinterpret_cast<const TestResult*>(resultBlob->getBufferPointer());

    CHECK_EQ(result->rayOrigin[0], 0.1f);
    CHECK_EQ(result->rayOrigin[1], 0.1f);
    CHECK_EQ(result->rayOrigin[2], 0.1f);
}

// Disabled under CUDA/OptiX and D3D12 due to https://github.com/shader-slang/slang/issues/8615
GPU_TEST_CASE("ray-tracing-hitobject-query-hit-ray-object-direction", ALL & ~CUDA & ~D3D12)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    RayTracingSingleTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(TestResult));
    test.run(
        "test-ray-tracing-hitobject-intrinsics",
        "rayGenShaderQueryRayObjectDirection",
        {"closestHitNOP"},
        {"missNOP"}
    );

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    const TestResult* result = reinterpret_cast<const TestResult*>(resultBlob->getBufferPointer());

    CHECK_EQ(result->rayDirection[0], 0.0f);
    CHECK_EQ(result->rayDirection[1], 0.0f);
    CHECK_EQ(result->rayDirection[2], 1.0f);
}

GPU_TEST_CASE("ray-tracing-hitobject-make-hit", ALL | DontCreateDevice)
{
    // Limit the shader model to SM 6.6 for this test, since the NVAPI headers don't support MakeHit
    // for newer shader models.
    DeviceExtraOptions extraOptions;
    extraOptions.d3d12HighestShaderModel = 0x66; // SM 6.6
    device = createTestingDevice(ctx, ctx->deviceType, false, &extraOptions);
    REQUIRE(device);

    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    // Disabled under pre OptiX 9.0 due to https://github.com/shader-slang/slang/issues/8723
    if (device->getDeviceType() == DeviceType::CUDA && device->getInfo().optixVersion < 90000)
        SKIP("MakeHit not functional with specified OptiX version");

    RayTracingSingleTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(TestResult));
    test.run(
        "test-ray-tracing-hitobject-intrinsics-make-hit",
        "rayGenShaderMakeQueryInvokeHit",
        {"closestHitInvoke"},
        {"missNOP"}
    );

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    checkQueryAndInvokeResult(resultBlob);
}
