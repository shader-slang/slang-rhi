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

struct RayTracingTriangleReorderTest
{
    IDevice* device;

    void init(IDevice* device_) { this->device = device_; }

    const uint32_t width = 128;
    const uint32_t height = 128;

    ComPtr<ITexture> resultTexture;

    void run(const char* raygenName)
    {
        ComPtr<ICommandQueue> queue = device->getQueue(QueueType::Graphics);

        ThreeTriangleBLAS blas(device, queue);
        TLAS tlas(device, queue, blas.blas);

        createResultTexture();

        RayTracingTestPipeline
            pipeline(device, "test-ray-tracing-reorder", {raygenName}, {{"closestHitShader", nullptr}}, {"missShader"});
        renderFrame(queue, pipeline.raytracingPipeline, pipeline.shaderTable, tlas.tlas);

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
        checkTestResults(expectedPixels);
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

GPU_TEST_CASE("ray-tracing-reorder-hint", ALL)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    RayTracingTriangleReorderTest test;
    test.init(device);
    test.run("rayGenShaderReorderHint");
}

GPU_TEST_CASE("ray-tracing-reorder-hit-obj", ALL)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    RayTracingTriangleReorderTest test;
    test.init(device);
    test.run("rayGenShaderReorderHitObj");
}

GPU_TEST_CASE("ray-tracing-reorder-hit-obj-and-hint", ALL)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    RayTracingTriangleReorderTest test;
    test.init(device);
    test.run("rayGenShaderReorderHitObjAndHint");
}
