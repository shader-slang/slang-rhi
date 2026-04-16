#include "testing.h"
#include "test-ray-tracing-common.h"

#include <array>

using namespace rhi;
using namespace rhi::testing;

namespace {

struct TransformResult
{
    float matrix[12];
};

struct RayTracingSingleTriangleTest
{
    IDevice* device = nullptr;

    void init(IDevice* device_) { device = device_; }

    ResultBuffer resultBuf;

    void createResultBuffer(size_t resultSize) { resultBuf = ResultBuffer(device, resultSize); }

    void run(
        const char* filepath,
        const char* raygenName,
        const std::vector<const char*>& closestHitNames,
        const std::vector<const char*>& missNames,
        const float* instanceTransform = nullptr
    )
    {
        ComPtr<ICommandQueue> queue = device->getQueue(QueueType::Graphics);

        SingleTriangleBLAS blas(device, queue);
        TLAS tlas(device, queue, blas.blas, instanceTransform);

        std::vector<HitGroupProgramNames> hitGroupProgramNames;
        for (const char* closestHitName : closestHitNames)
            hitGroupProgramNames.push_back({closestHitName, /*anyhit=*/nullptr, /*intersection=*/nullptr});

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

constexpr std::array<float, 12> kInstanceTransform = {
    1.0f,
    0.0f,
    0.0f,
    1.0f,
    0.0f,
    1.0f,
    0.0f,
    2.0f,
    0.0f,
    0.0f,
    1.0f,
    3.0f,
};

constexpr std::array<float, 12> kObjectToWorld3x4 = kInstanceTransform;

constexpr std::array<float, 12> kWorldToObject3x4 = {
    1.0f,
    0.0f,
    0.0f,
    -1.0f,
    0.0f,
    1.0f,
    0.0f,
    -2.0f,
    0.0f,
    0.0f,
    1.0f,
    -3.0f,
};

constexpr std::array<float, 12> kObjectToWorld4x3 = {
    1.0f,
    0.0f,
    0.0f,
    0.0f,
    1.0f,
    0.0f,
    0.0f,
    0.0f,
    1.0f,
    1.0f,
    2.0f,
    3.0f,
};

constexpr std::array<float, 12> kWorldToObject4x3 = {
    1.0f,
    0.0f,
    0.0f,
    0.0f,
    1.0f,
    0.0f,
    0.0f,
    0.0f,
    1.0f,
    -1.0f,
    -2.0f,
    -3.0f,
};

void checkMatrix(const TransformResult* result, const std::array<float, 12>& expected)
{
    for (size_t i = 0; i < expected.size(); ++i)
        CHECK_EQ(result->matrix[i], expected[i]);
}

} // namespace

// Disabled under CUDA/OptiX due to https://github.com/shader-slang/slang/issues/9256
GPU_TEST_CASE("ray-tracing-transform-object-to-world-3x4", ALL & ~CUDA)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");

    RayTracingSingleTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(TransformResult));
    test.run(
        "test-ray-tracing-transform-query",
        "rayGenShaderObjectToWorld3x4",
        {"closestHitObjectToWorld3x4"},
        {"missNOP"},
        kInstanceTransform.data()
    );

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    const TransformResult* result = reinterpret_cast<const TransformResult*>(resultBlob->getBufferPointer());
    checkMatrix(result, kObjectToWorld3x4);
}

// Disabled under CUDA/OptiX due to https://github.com/shader-slang/slang/issues/9256
GPU_TEST_CASE("ray-tracing-transform-world-to-object-3x4", ALL & ~CUDA)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");

    RayTracingSingleTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(TransformResult));
    test.run(
        "test-ray-tracing-transform-query",
        "rayGenShaderWorldToObject3x4",
        {"closestHitWorldToObject3x4"},
        {"missNOP"},
        kInstanceTransform.data()
    );

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    const TransformResult* result = reinterpret_cast<const TransformResult*>(resultBlob->getBufferPointer());
    checkMatrix(result, kWorldToObject3x4);
}

// Disabled under CUDA/OptiX due to https://github.com/shader-slang/slang/issues/9256
GPU_TEST_CASE("ray-tracing-transform-object-to-world-4x3", ALL & ~CUDA)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");

    RayTracingSingleTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(TransformResult));
    test.run(
        "test-ray-tracing-transform-query",
        "rayGenShaderObjectToWorld4x3",
        {"closestHitObjectToWorld4x3"},
        {"missNOP"},
        kInstanceTransform.data()
    );

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    const TransformResult* result = reinterpret_cast<const TransformResult*>(resultBlob->getBufferPointer());
    checkMatrix(result, kObjectToWorld4x3);
}

// Disabled under CUDA/OptiX due to https://github.com/shader-slang/slang/issues/9256
GPU_TEST_CASE("ray-tracing-transform-world-to-object-4x3", ALL & ~CUDA)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");

    RayTracingSingleTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(TransformResult));
    test.run(
        "test-ray-tracing-transform-query",
        "rayGenShaderWorldToObject4x3",
        {"closestHitWorldToObject4x3"},
        {"missNOP"},
        kInstanceTransform.data()
    );

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    const TransformResult* result = reinterpret_cast<const TransformResult*>(resultBlob->getBufferPointer());
    checkMatrix(result, kWorldToObject4x3);
}

// Disabled under CUDA/OptiX due to https://github.com/shader-slang/slang/issues/9256
// Disabled under D3D12 due to https://github.com/shader-slang/slang/issues/9257
GPU_TEST_CASE("ray-tracing-transform-hitobject-world-to-object", ALL & ~CUDA & ~D3D12)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    RayTracingSingleTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(TransformResult));
    test.run(
        "test-ray-tracing-transform-query",
        "rayGenShaderHitObjectGetWorldToObject",
        {"closestHitNOP"},
        {"missNOP"},
        kInstanceTransform.data()
    );

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    const TransformResult* result = reinterpret_cast<const TransformResult*>(resultBlob->getBufferPointer());
    checkMatrix(result, kWorldToObject4x3);
}

// Disabled under CUDA/OptiX because it isn't implemented.
// Disabled under D3D12 due to https://github.com/shader-slang/slang/issues/9257
GPU_TEST_CASE("ray-tracing-transform-hitobject-object-to-world", ALL & ~CUDA & ~D3D12)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    RayTracingSingleTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(TransformResult));
    test.run(
        "test-ray-tracing-transform-query",
        "rayGenShaderHitObjectGetObjectToWorld",
        {"closestHitNOP"},
        {"missNOP"},
        kInstanceTransform.data()
    );

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    const TransformResult* result = reinterpret_cast<const TransformResult*>(resultBlob->getBufferPointer());
    checkMatrix(result, kObjectToWorld4x3);
}
