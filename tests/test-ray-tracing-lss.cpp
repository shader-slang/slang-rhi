#include "testing.h"
#include "test-ray-tracing-common.h"

#include <slang-rhi/acceleration-structure-utils.h>

using namespace rhi;
using namespace rhi::testing;

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
struct RayTracingLssTest
{
    IDevice* device;

    void init(IDevice* device_) { this->device = device_; }

    ComPtr<ITexture> resultTexture;

    uint32_t width = 128;
    uint32_t height = 128;

    void run()
    {
        ComPtr<ICommandQueue> queue = device->getQueue(QueueType::Graphics);

        TwoSegmentLssBLAS blas(device, queue);
        TLAS tlas(device, queue, blas.blas);

        createResultTexture();

        // OptiX requires an intersection shader for non-triangle geometry.
        const char* intersectionName =
            device->getDeviceType() == DeviceType::CUDA ? "__builtin_intersection__linear_swept_spheres" : nullptr;

        RayTracingTestPipeline pipeline(
            device,
            "test-ray-tracing-lss",
            {"rayGenShader"},
            {{"closestHitShader", /*anyhit=*/nullptr, intersectionName}},
            {"missShader"},
            RayTracingPipelineFlags::EnableLinearSweptSpheres
        );
        renderFrame(queue, pipeline.raytracingPipeline, pipeline.shaderTable, tlas.tlas);

        ExpectedPixel expectedPixels[] = {
            EXPECTED_PIXEL(32, 32, 1.f, 0.f, 0.f, 1.f), // Segment 1, top left
            EXPECTED_PIXEL(96, 32, 0.f, 1.f, 0.f, 1.f), // Segment 2, top right

            // Corners should all be misses
            EXPECTED_PIXEL(0, 0, 1.f, 1.0f, 1.0f, 1.0f),     // Miss
            EXPECTED_PIXEL(127, 0, 1.f, 1.0f, 1.0f, 1.0f),   // Miss
            EXPECTED_PIXEL(127, 127, 1.f, 1.0f, 1.0f, 1.0f), // Miss
            EXPECTED_PIXEL(0, 127, 1.f, 1.0f, 1.0f, 1.0f),   // Miss

            // Center between segments should be a miss
            EXPECTED_PIXEL(64, 32, 1.f, 1.0f, 1.0f, 1.0f),
        };
        checkTestResults(expectedPixels);
    }

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
        writeImage("test-ray-tracing-lss-intersection.hdr", resultBlob, width, height, layout.rowPitch, layout.colPitch);
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
        IRayTracingPipeline* raytracingPipeline,
        IShaderTable* shaderTable,
        IAccelerationStructure* tlas
    )
    {
        auto commandEncoder = queue->createCommandEncoder();

        auto passEncoder = commandEncoder->beginRayTracingPass();
        auto rootObject = passEncoder->bindPipeline(raytracingPipeline, shaderTable);
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
};

GPU_TEST_CASE("ray-tracing-lss-intersection", ALL)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::AccelerationStructureLinearSweptSpheres))
        SKIP("acceleration structure linear swept spheres not supported");

    RayTracingLssTest test;
    test.init(device);
    test.run();
}

struct TestResult
{
    int isLssHit;
    float lssPositionsAndRadii[8];
};

struct TestResultCudaAligned
{
    int isLssHit;
    int pad[3];
    float lssPositionsAndRadii[8];
};

struct RayTracingLssIntrinsicsTest
{
    IDevice* device;

    void init(IDevice* device_) { this->device = device_; }

    void run(const char* raygenName, const char* closestHitName)
    {
        ComPtr<ICommandQueue> queue = device->getQueue(QueueType::Graphics);

        const size_t resultSize =
            device->getDeviceType() == DeviceType::CUDA ? sizeof(TestResultCudaAligned) : sizeof(TestResult);
        ResultBuffer resultBuf(device, resultSize);

        SingleSegmentLssBLAS blas(device, queue);
        TLAS tlas(device, queue, blas.blas);

        // OptiX requires an intersection shader for non-triangle geometry.
        const char* intersectionName =
            device->getDeviceType() == DeviceType::CUDA ? "__builtin_intersection__linear_swept_spheres" : nullptr;

        RayTracingTestPipeline pipeline(
            device,
            "test-ray-tracing-lss",
            {raygenName},
            {{closestHitName, /*anyhit=*/nullptr, intersectionName}},
            {"missNOP"},
            RayTracingPipelineFlags::EnableLinearSweptSpheres
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

        CHECK_EQ(result->isLssHit, 1);

        // Left endcap position
        CHECK_EQ(result->lssPositionsAndRadii[0], -0.5f);
        CHECK_EQ(result->lssPositionsAndRadii[1], 0.0f);
        CHECK_EQ(result->lssPositionsAndRadii[2], -3.0f);

        // Left endcap radius
        CHECK_EQ(result->lssPositionsAndRadii[3], 0.5f);

        // Right endcap position
        CHECK_EQ(result->lssPositionsAndRadii[4], 0.5f);
        CHECK_EQ(result->lssPositionsAndRadii[5], 0.0f);
        CHECK_EQ(result->lssPositionsAndRadii[6], -3.0f);

        // Right endcap radius
        CHECK_EQ(result->lssPositionsAndRadii[7], 0.5f);
    }
};

GPU_TEST_CASE("ray-tracing-lss-intrinsics", ALL)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::AccelerationStructureLinearSweptSpheres))
        SKIP("acceleration structure linear swept spheres not supported");

    RayTracingLssIntrinsicsTest test;
    test.init(device);
    test.run("rayGenLssIntrinsics", "closestHitLssIntrinsics");
}

GPU_TEST_CASE("ray-tracing-lss-intrinsics-hit-object", ALL)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::AccelerationStructureLinearSweptSpheres))
        SKIP("acceleration structure linear swept spheres not supported");

    RayTracingLssIntrinsicsTest test;
    test.init(device);
    test.run("rayGenLssIntrinsicsHitObject", "closestHitNOP");
}
