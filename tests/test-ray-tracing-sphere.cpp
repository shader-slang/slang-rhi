#include "testing.h"
#include "test-ray-tracing-common.h"

#include <slang-rhi/acceleration-structure-utils.h>

using namespace rhi;
using namespace rhi::testing;

namespace {
struct ExpectedPixel
{
    uint32_t pos[2];
    float color[4];
};

#define EXPECTED_PIXEL(x, y, r, g, b, a)                                                                               \
    {                                                                                                                  \
        {x, y},                                                                                                        \
        {                                                                                                              \
            r, g, b, a                                                                                                 \
        }                                                                                                              \
    }

// Test that the ray tracing pipeline can perform sphere intersection.
struct RayTracingSphereIntersectionTest
{
    IDevice* device;

    void init(IDevice* device_) { this->device = device_; }

    ComPtr<ITexture> resultTexture;

    const uint32_t width = 128;
    const uint32_t height = 128;

    void createResultTexture()
    {
        TextureDesc resultTextureDesc = {};
        resultTextureDesc.type = TextureType::Texture2D;
        resultTextureDesc.mipCount = 1;
        resultTextureDesc.size.width = width;
        resultTextureDesc.size.height = height;
        resultTextureDesc.size.depth = 1;
        resultTextureDesc.usage = TextureUsage::UnorderedAccess | TextureUsage::CopySource;
        resultTextureDesc.defaultState = ResourceState::UnorderedAccess;
        resultTextureDesc.format = Format::RGBA32Float;
        resultTexture = device->createTexture(resultTextureDesc);
    }

    void checkTestResults(span<ExpectedPixel> expectedPixels)
    {
        ComPtr<ISlangBlob> resultBlob;
        SubresourceLayout layout;
        REQUIRE_CALL(device->readTexture(resultTexture, 0, 0, resultBlob.writeRef(), &layout));
#if 0 // for debugging only
        writeImage("test-ray-tracing-sphere-intersection.hdr", resultBlob, width, height, layout.rowPitch, layout.colPitch);
#endif

        for (const auto& ep : expectedPixels)
        {
            uint32_t x = ep.pos[0];
            uint32_t y = ep.pos[1];
            const float* color = reinterpret_cast<const float*>(
                static_cast<const uint8_t*>(resultBlob->getBufferPointer()) + y * layout.rowPitch + x * layout.colPitch
            );
            CAPTURE(x);
            CAPTURE(y);
            CHECK_EQ(color[0], ep.color[0]);
            CHECK_EQ(color[1], ep.color[1]);
            CHECK_EQ(color[2], ep.color[2]);
            CHECK_EQ(color[3], ep.color[3]);
        }
    }

    void renderFrame(
        ICommandQueue* queue,
        IRayTracingPipeline* pipeline,
        IShaderTable* shaderTable,
        IAccelerationStructure* tlas
    )
    {
        auto commandEncoder = queue->createCommandEncoder();

        auto passEncoder = commandEncoder->beginRayTracingPass();
        auto rootObject = passEncoder->bindPipeline(pipeline, shaderTable);
        auto cursor = ShaderCursor(rootObject);
        uint32_t dims[2] = {width, height};
        cursor["dims"].setData(dims, sizeof(dims));
        cursor["resultTexture"].setBinding(resultTexture);
        cursor["sceneBVH"].setBinding(tlas);
        passEncoder->dispatchRays(0, width, height, 1);
        passEncoder->end();

        queue->submit(commandEncoder->finish());
        queue->waitOnHost();
    }

    void run()
    {
        ComPtr<ICommandQueue> queue = device->getQueue(QueueType::Graphics);

        ThreeSphereBLAS blas(device, queue);
        TLAS tlas(device, queue, blas.blas);

        std::vector<const char*> raygenNames = {"rayGenShader"};

        // OptiX requires an intersection shader for non-triangle geometry.
        const char* intersectionName =
            device->getDeviceType() == DeviceType::CUDA ? "__builtin_intersection__sphere" : nullptr;

        std::vector<HitGroupProgramNames> hitGroupProgramNames = {
            {"closestHitShader", /*anyhit=*/nullptr, intersectionName},
        };
        std::vector<const char*> missNames = {"missShader"};

        createResultTexture();

        RayTracingTestPipeline pipeline(
            device,
            "test-ray-tracing-sphere",
            raygenNames,
            hitGroupProgramNames,
            missNames,
            RayTracingPipelineFlags::EnableSpheres
        );
        renderFrame(queue, pipeline.raytracingPipeline, pipeline.shaderTable, tlas.tlas);

        ExpectedPixel expectedPixels[] = {
            EXPECTED_PIXEL(32, 32, 1.f, 0.f, 0.f, 1.f), // Sphere 1
            EXPECTED_PIXEL(96, 32, 0.f, 1.f, 0.f, 1.f), // Sphere 2
            EXPECTED_PIXEL(64, 96, 0.f, 0.f, 1.f, 1.f), // Sphere 3

            // Corners should all be misses
            EXPECTED_PIXEL(0, 0, 1.f, 1.0f, 1.0f, 1.0f),     // Miss
            EXPECTED_PIXEL(127, 0, 1.f, 1.0f, 1.0f, 1.0f),   // Miss
            EXPECTED_PIXEL(127, 127, 1.f, 1.0f, 1.0f, 1.0f), // Miss
            EXPECTED_PIXEL(0, 127, 1.f, 1.0f, 1.0f, 1.0f),   // Miss
        };
        checkTestResults(expectedPixels);
    }
};
} // namespace

GPU_TEST_CASE("ray-tracing-sphere-intersection", ALL)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::AccelerationStructureSpheres))
        SKIP("acceleration structure spheres not supported");

    RayTracingSphereIntersectionTest test;
    test.init(device);
    test.run();
}

namespace {
struct TestResult
{
    int isSphereHit;
    float spherePositionAndRadius[4];
};

struct TestResultCudaAligned
{
    int isSphereHit;
    int pad[3];
    float spherePositionAndRadius[4];
};

struct RayTracingSphereIntrinsicsTest
{
    IDevice* device;

    void init(IDevice* device_) { this->device = device_; }

    void run(const char* raygenName, const char* closestHitName)
    {
        ComPtr<ICommandQueue> queue = device->getQueue(QueueType::Graphics);

        SingleSphereBLAS blas(device, queue);
        TLAS tlas(device, queue, blas.blas);

        const char* intersectionName =
            device->getDeviceType() == DeviceType::CUDA ? "__builtin_intersection__sphere" : nullptr;

        std::vector<HitGroupProgramNames> hitGroupProgramNames = {
            {closestHitName, /*anyhit=*/nullptr, intersectionName},
        };
        std::vector<const char*> missNames = {"missNOP"};

        size_t resultSize =
            device->getDeviceType() == DeviceType::CUDA ? sizeof(TestResultCudaAligned) : sizeof(TestResult);
        ResultBuffer resultBuf(device, resultSize);

        RayTracingTestPipeline pipeline(
            device,
            "test-ray-tracing-sphere",
            {raygenName},
            hitGroupProgramNames,
            missNames,
            RayTracingPipelineFlags::EnableSpheres
        );
        launchPipeline(queue, pipeline.raytracingPipeline, pipeline.shaderTable, resultBuf.resultBuffer, tlas.tlas);

        ComPtr<ISlangBlob> resultBlob;
        resultBuf.getFromDevice(resultBlob.writeRef());

        if (device->getDeviceType() == DeviceType::CUDA)
            checkTestResults<TestResultCudaAligned>(resultBlob);
        else
            checkTestResults<TestResult>(resultBlob);
    }

    template<typename T>
    void checkTestResults(ISlangBlob* resultBlob)
    {
        const T* result = reinterpret_cast<const T*>(resultBlob->getBufferPointer());
        CHECK_EQ(result->isSphereHit, 1);
        CHECK_EQ(result->spherePositionAndRadius[0], 0.0f);
        CHECK_EQ(result->spherePositionAndRadius[1], 0.0f);
        CHECK_EQ(result->spherePositionAndRadius[2], -3.0f);
        CHECK_EQ(result->spherePositionAndRadius[3], 2.0f);
    }
};
} // namespace

GPU_TEST_CASE("ray-tracing-sphere-intrinsics", ALL)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::AccelerationStructureSpheres))
        SKIP("acceleration structure spheres not supported");

    RayTracingSphereIntrinsicsTest test;
    test.init(device);
    test.run("rayGenSphereIntrinsics", "closestHitSphereIntrinsics");
}

// Disabled under D3D12 due to https://github.com/shader-slang/slang/issues/8128
GPU_TEST_CASE("ray-tracing-sphere-intrinsics-hit-object", ALL & ~D3D12)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::AccelerationStructureSpheres))
        SKIP("acceleration structure spheres not supported");

    RayTracingSphereIntrinsicsTest test;
    test.init(device);
    test.run("rayGenSphereIntrinsicsHitObject", "closestHitNOP");
}
