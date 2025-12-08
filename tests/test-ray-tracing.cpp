#include "testing.h"
#include "test-ray-tracing-common.h"
#include "texture-utils.h"

#include <slang-rhi/acceleration-structure-utils.h>
#include <memory>

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

struct RayTracingTriangleIntersectionTest
{
    IDevice* device;

    void init(IDevice* device_) { this->device = device_; }

    const uint32_t width = 128;
    const uint32_t height = 128;

    ComPtr<ITexture> resultTexture;

    void run(
        IAccelerationStructure* tlas,
        const std::vector<const char*>& raygenNames,
        const std::vector<HitGroupProgramNames>& hitGroupProgramNames,
        const std::vector<const char*>& missNames,
        span<ExpectedPixel> expectedPixels,
        unsigned rgIdx = 0,
        RayTracingPipelineFlags flags = RayTracingPipelineFlags::None
    )
    {
        ComPtr<ICommandQueue> queue = device->getQueue(QueueType::Graphics);

        createResultTexture();

        RayTracingTestPipeline
            pipeline(device, "test-ray-tracing", raygenNames, hitGroupProgramNames, missNames, flags);
        renderFrame(queue, pipeline.raytracingPipeline, pipeline.shaderTable, tlas, rgIdx);

        checkTestResults(expectedPixels);
    }

    void renderFrame(
        ICommandQueue* queue,
        IRayTracingPipeline* pipeline,
        IShaderTable* shaderTable,
        IAccelerationStructure* tlas,
        unsigned rgIdx
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
        passEncoder->dispatchRays(rgIdx, width, height, 1);
        passEncoder->end();

        queue->submit(commandEncoder->finish());
        queue->waitOnHost();
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
        writeImage("test.hdr", resultBlob, width, height, layout.rowPitch, layout.colPitch);
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
};

} // namespace

GPU_TEST_CASE("ray-tracing-triangle-intersection", ALL)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");

    ComPtr<ICommandQueue> queue = device->getQueue(QueueType::Graphics);
    ThreeTriangleBLAS blas(device, queue);
    TLAS tlas(device, queue, blas.blas);

    std::vector<const char*> raygenNames = {"rayGenShaderIdx0", "rayGenShaderIdx1"};
    std::vector<HitGroupProgramNames> hitGroupProgramNames = {
        {"closestHitShaderIdx0", /*anyhit=*/nullptr, /*intersection=*/nullptr},
        {"closestHitShaderIdx1", /*anyhit=*/nullptr, /*intersection=*/nullptr},
    };
    std::vector<const char*> missNames = {"missShaderIdx0", "missShaderIdx1"};

    ExpectedPixel expectedPixels[] = {
        EXPECTED_PIXEL(64, 64, 1.f, 0.f, 0.f, 1.f), // Triangle 1
        EXPECTED_PIXEL(63, 64, 0.f, 1.f, 0.f, 1.f), // Triangle 2
        EXPECTED_PIXEL(64, 63, 0.f, 0.f, 1.f, 1.f), // Triangle 3
        EXPECTED_PIXEL(63, 63, 1.f, 1.f, 1.f, 1.f), // Miss
        // Corners should all be misses
        EXPECTED_PIXEL(0, 0, 1.f, 1.f, 1.f, 1.f),     // Miss
        EXPECTED_PIXEL(127, 0, 1.f, 1.f, 1.f, 1.f),   // Miss
        EXPECTED_PIXEL(127, 127, 1.f, 1.f, 1.f, 1.f), // Miss
        EXPECTED_PIXEL(0, 127, 1.f, 1.f, 1.f, 1.f),   // Miss
    };

    RayTracingTriangleIntersectionTest test;
    test.init(device);
    test.run(tlas.tlas, raygenNames, hitGroupProgramNames, missNames, expectedPixels);
}

GPU_TEST_CASE("ray-tracing-triangle-intersection-nonzero-rg-idx", ALL)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");

    ComPtr<ICommandQueue> queue = device->getQueue(QueueType::Graphics);
    ThreeTriangleBLAS blas(device, queue);
    TLAS tlas(device, queue, blas.blas);

    std::vector<const char*> raygenNames = {"rayGenShaderIdx0", "rayGenShaderIdx1"};
    std::vector<HitGroupProgramNames> hitGroupProgramNames = {
        {"closestHitShaderIdx0", /*anyhit=*/nullptr, /*intersection=*/nullptr},
        {"closestHitShaderIdx1", /*anyhit=*/nullptr, /*intersection=*/nullptr},
    };
    std::vector<const char*> missNames = {"missShaderIdx0", "missShaderIdx1"};

    ExpectedPixel expectedPixels[] = {
        EXPECTED_PIXEL(64, 64, 0.f, 1.f, 1.f, 1.f), // Triangle 1
        EXPECTED_PIXEL(63, 64, 1.f, 0.f, 1.f, 1.f), // Triangle 2
        EXPECTED_PIXEL(64, 63, 1.f, 1.f, 0.f, 1.f), // Triangle 3
        EXPECTED_PIXEL(63, 63, 0.f, 0.f, 0.f, 1.f), // Miss
        // Corners should all be misses
        EXPECTED_PIXEL(0, 0, 0.f, 0.f, 0.f, 1.f),     // Miss
        EXPECTED_PIXEL(127, 0, 0.f, 0.f, 0.f, 1.f),   // Miss
        EXPECTED_PIXEL(127, 127, 0.f, 0.f, 0.f, 1.f), // Miss
        EXPECTED_PIXEL(0, 127, 0.f, 0.f, 0.f, 1.f),   // Miss
    };

    RayTracingTriangleIntersectionTest test;
    test.init(device);
    test.run(tlas.tlas, raygenNames, hitGroupProgramNames, missNames, expectedPixels, 1);
}

GPU_TEST_CASE("ray-tracing-triangle-intersection-vertex-motion", ALL)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::RayTracingMotionBlur))
        SKIP("ray tracing motion blur not supported");

    ComPtr<ICommandQueue> queue = device->getQueue(QueueType::Graphics);
    SingleTriangleVertexMotionBLAS blas(device, queue);
    VertexMotionInstanceTLAS tlas(device, queue, blas.blas, 2);

    std::vector<const char*> raygenNames = {"rayGenShaderMotion"};
    std::vector<HitGroupProgramNames> hitGroupProgramNames = {
        {"closestHitShaderMotion", /*anyhit=*/nullptr, /*intersection=*/nullptr},
    };
    std::vector<const char*> missNames = {"missShaderIdx0"};

    // At time=0.5, triangle is halfway rotated (45 degrees)
    // The triangle at time=0.0 is at (0,0,1), (1,0,1), (0,1,1)
    // The triangle at time=1.0 is at (0,0,1), (0,1,1), (-1,0,1)
    // Testing a few specific rays that should hit or miss
    ExpectedPixel expectedPixels[] = {
        // Hits near each corner of the triangle
        EXPECTED_PIXEL(64, 66, 0.f, 1.f, 0.f, 1.f), // Should hit the triangle (green)
        EXPECTED_PIXEL(36, 94, 0.f, 1.f, 0.f, 1.f), // Should hit (green)
        EXPECTED_PIXEL(90, 94, 0.f, 1.f, 0.f, 1.f), // Should hit (green)

        // Corners should all be misses
        EXPECTED_PIXEL(0, 0, 1.f, 1.f, 1.f, 1.f),     // Miss
        EXPECTED_PIXEL(127, 0, 1.f, 1.f, 1.f, 1.f),   // Miss
        EXPECTED_PIXEL(127, 127, 1.f, 1.f, 1.f, 1.f), // Miss
        EXPECTED_PIXEL(0, 127, 1.f, 1.f, 1.f, 1.f),   // Miss
    };

    RayTracingTriangleIntersectionTest test;
    test.init(device);
    test.run(
        tlas.tlas,
        raygenNames,
        hitGroupProgramNames,
        missNames,
        expectedPixels,
        0,
        RayTracingPipelineFlags::EnableMotion
    );
}

GPU_TEST_CASE("ray-tracing-triangle-intersection-matrix-motion", ALL)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::RayTracingMotionBlur))
        SKIP("ray tracing motion blur not supported");

    ComPtr<ICommandQueue> queue = device->getQueue(QueueType::Graphics);
    SingleTriangleBLAS blas(device, queue);
    MatrixMotionInstanceTLAS tlas(device, queue, blas.blas, 2);

    std::vector<const char*> raygenNames = {"rayGenShaderMotion"};
    std::vector<HitGroupProgramNames> hitGroupProgramNames = {
        {"closestHitShaderMotion", /*anyhit=*/nullptr, /*intersection=*/nullptr},
    };
    std::vector<const char*> missNames = {"missShaderIdx0"};

    ExpectedPixel expectedPixels[] = {
        // Hits near each corner of the triangle
        EXPECTED_PIXEL(64, 66, 0.f, 1.f, 0.f, 1.f),  // Should hit the triangle (green)
        EXPECTED_PIXEL(36, 94, 0.f, 1.f, 0.f, 1.f),  // Should hit (green)
        EXPECTED_PIXEL(34, 120, 0.f, 1.f, 0.f, 1.f), // Should hit (green)

        // Corners should all be misses
        EXPECTED_PIXEL(0, 0, 1.f, 1.f, 1.f, 1.f),     // Miss
        EXPECTED_PIXEL(127, 0, 1.f, 1.f, 1.f, 1.f),   // Miss
        EXPECTED_PIXEL(127, 127, 1.f, 1.f, 1.f, 1.f), // Miss
        EXPECTED_PIXEL(0, 127, 1.f, 1.f, 1.f, 1.f),   // Miss
    };

    RayTracingTriangleIntersectionTest test;
    test.init(device);
    test.run(
        tlas.tlas,
        raygenNames,
        hitGroupProgramNames,
        missNames,
        expectedPixels,
        0,
        RayTracingPipelineFlags::EnableMotion
    );
}

GPU_TEST_CASE("ray-tracing-triangle-intersection-srt-motion", ALL)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::RayTracingMotionBlur))
        SKIP("ray tracing motion blur not supported");

    ComPtr<ICommandQueue> queue = device->getQueue(QueueType::Graphics);
    SingleTriangleBLAS blas(device, queue);
    SrtMotionInstanceTLAS tlas(device, queue, blas.blas, 2);

    std::vector<const char*> raygenNames = {"rayGenShaderMotion"};
    std::vector<HitGroupProgramNames> hitGroupProgramNames = {
        {"closestHitShaderMotion", /*anyhit=*/nullptr, /*intersection=*/nullptr},
    };
    std::vector<const char*> missNames = {"missShaderIdx0"};

    ExpectedPixel expectedPixels[] = {
        // Hits near each corner of the triangle
        EXPECTED_PIXEL(64, 66, 0.f, 1.f, 0.f, 1.f),  // Should hit the triangle (green)
        EXPECTED_PIXEL(36, 94, 0.f, 1.f, 0.f, 1.f),  // Should hit (green)
        EXPECTED_PIXEL(34, 120, 0.f, 1.f, 0.f, 1.f), // Should hit (green)

        // Corners should all be misses
        EXPECTED_PIXEL(0, 0, 1.f, 1.f, 1.f, 1.f),     // Miss
        EXPECTED_PIXEL(127, 0, 1.f, 1.f, 1.f, 1.f),   // Miss
        EXPECTED_PIXEL(127, 127, 1.f, 1.f, 1.f, 1.f), // Miss
        EXPECTED_PIXEL(0, 127, 1.f, 1.f, 1.f, 1.f),   // Miss
    };

    RayTracingTriangleIntersectionTest test;
    test.init(device);
    test.run(
        tlas.tlas,
        raygenNames,
        hitGroupProgramNames,
        missNames,
        expectedPixels,
        0,
        RayTracingPipelineFlags::EnableMotion
    );
}
