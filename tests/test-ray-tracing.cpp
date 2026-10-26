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
        std::span<ExpectedPixel> expectedPixels,
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

    void checkTestResults(std::span<ExpectedPixel> expectedPixels)
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
    SKIP_IF_ARM64_WARP(device);

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

GPU_TEST_CASE("ray-tracing-native-primitive-flags", ALL)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    SKIP_IF_ARM64_WARP(device);

    ComPtr<ICommandQueue> queue = device->getQueue(QueueType::Graphics);
    ThreeTriangleBLAS blas(device, queue);
    TLAS tlas(device, queue, blas.blas);

    const RayTracingPipelineFlags flagCombinations[] = {
        RayTracingPipelineFlags::None,
        RayTracingPipelineFlags::EnableSpheres,
        RayTracingPipelineFlags::EnableLinearSweptSpheres,
        RayTracingPipelineFlags::EnableSpheres | RayTracingPipelineFlags::EnableLinearSweptSpheres,
    };
    for (auto flags : flagCombinations)
    {
        if (is_set(flags, RayTracingPipelineFlags::EnableSpheres) &&
            !device->hasFeature(Feature::AccelerationStructureSpheres))
            continue;
        if (is_set(flags, RayTracingPipelineFlags::EnableLinearSweptSpheres) &&
            !device->hasFeature(Feature::AccelerationStructureLinearSweptSpheres))
            continue;
        CAPTURE(uint32_t(flags));

        // Enabling native primitives must preserve custom intersection shader linking and triangle traversal.
        std::vector<HitGroupProgramNames> hitGroupProgramNames = {
            {"closestHitShaderIdx0", nullptr, nullptr},
            {nullptr, nullptr, "intersectionShaderNoHit"},
        };
        ExpectedPixel expectedPixels[] = {
            EXPECTED_PIXEL(64, 64, 1.f, 0.f, 0.f, 1.f),
            EXPECTED_PIXEL(63, 64, 0.f, 1.f, 0.f, 1.f),
            EXPECTED_PIXEL(64, 63, 0.f, 0.f, 1.f, 1.f),
            EXPECTED_PIXEL(63, 63, 1.f, 1.f, 1.f, 1.f),
        };
        RayTracingTriangleIntersectionTest test;
        test.init(device);
        test.run(tlas.tlas, {"rayGenShaderIdx0"}, hitGroupProgramNames, {"missShaderIdx0"}, expectedPixels, 0, flags);

        // Trace each enabled native type as well, including when both flags are set.
        // OptiX requires a built-in intersection shader for native non-triangle geometry.
        if (is_set(flags, RayTracingPipelineFlags::EnableSpheres))
        {
            ThreeSphereBLAS sphere_blas(device, queue);
            TLAS sphere_tlas(device, queue, sphere_blas.blas);
            hitGroupProgramNames[0].intersection =
                device->getDeviceType() == DeviceType::CUDA ? "__builtin_intersection__sphere" : nullptr;
            ExpectedPixel sphere_pixels[] = {
                EXPECTED_PIXEL(32, 32, 1.f, 0.f, 0.f, 1.f),
                EXPECTED_PIXEL(96, 32, 0.f, 1.f, 0.f, 1.f),
                EXPECTED_PIXEL(64, 96, 0.f, 0.f, 1.f, 1.f),
                EXPECTED_PIXEL(0, 0, 1.f, 1.f, 1.f, 1.f),
            };
            test.run(
                sphere_tlas.tlas,
                {"rayGenShaderIdx0"},
                hitGroupProgramNames,
                {"missShaderIdx0"},
                sphere_pixels,
                0,
                flags
            );
        }
        if (is_set(flags, RayTracingPipelineFlags::EnableLinearSweptSpheres))
        {
            TwoSegmentLssBLAS lss_blas(device, queue);
            TLAS lss_tlas(device, queue, lss_blas.blas);
            hitGroupProgramNames[0].intersection =
                device->getDeviceType() == DeviceType::CUDA ? "__builtin_intersection__linear_swept_spheres" : nullptr;
            ExpectedPixel lss_pixels[] = {
                EXPECTED_PIXEL(32, 32, 1.f, 0.f, 0.f, 1.f),
                EXPECTED_PIXEL(96, 32, 0.f, 1.f, 0.f, 1.f),
                EXPECTED_PIXEL(64, 32, 1.f, 1.f, 1.f, 1.f),
                EXPECTED_PIXEL(0, 0, 1.f, 1.f, 1.f, 1.f),
            };
            test.run(
                lss_tlas.tlas,
                {"rayGenShaderIdx0"},
                hitGroupProgramNames,
                {"missShaderIdx0"},
                lss_pixels,
                0,
                flags
            );
        }
    }
}

GPU_TEST_CASE("ray-tracing-triangle-intersection-nonzero-rg-idx", ALL)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    SKIP_IF_ARM64_WARP(device);

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
    SKIP_IF_ARM64_WARP(device);

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
    SKIP_IF_ARM64_WARP(device);

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
    SKIP_IF_ARM64_WARP(device);

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

// Build a triangle BLAS whose index count is smaller than its vertex count and check that only the
// triangles defined by the indices are part of the acceleration structure.
GPU_TEST_CASE("ray-tracing-triangle-index-count", D3D12 | Vulkan | Metal)
{
    if (!device->hasFeature(Feature::RayQuery))
        SKIP("ray query not supported");
    SKIP_IF_ARM64_WARP(device);

    ComPtr<ICommandQueue> queue = device->getQueue(QueueType::Graphics);

    // Two triangles: one around (0.25, 0.25) and one around (-0.75, 0.25).
    const Vertex vertices[] = {
        {0.f, 0.f, 1.f},
        {1.f, 0.f, 1.f},
        {0.f, 1.f, 1.f},
        {-1.f, 0.f, 1.f},
        {0.f, 0.f, 1.f},
        {-1.f, 1.f, 1.f},
    };
    // The index buffer holds indices for both triangles, but the build only uses the first three. Building
    // with more triangles than indicated by the index count would include the second triangle.
    const uint32_t indices[] = {0, 1, 2, 3, 4, 5};
    const uint32_t vertexCount = SLANG_COUNT_OF(vertices);
    const uint32_t indexCount = 3;

    BufferDesc vertexBufferDesc;
    vertexBufferDesc.size = sizeof(vertices);
    vertexBufferDesc.usage = BufferUsage::AccelerationStructureBuildInput;
    vertexBufferDesc.defaultState = ResourceState::AccelerationStructureBuildInput;
    ComPtr<IBuffer> vertexBuffer = device->createBuffer(vertexBufferDesc, vertices);
    REQUIRE(vertexBuffer != nullptr);

    BufferDesc indexBufferDesc;
    indexBufferDesc.size = sizeof(indices);
    indexBufferDesc.usage = BufferUsage::AccelerationStructureBuildInput;
    indexBufferDesc.defaultState = ResourceState::AccelerationStructureBuildInput;
    ComPtr<IBuffer> indexBuffer = device->createBuffer(indexBufferDesc, indices);
    REQUIRE(indexBuffer != nullptr);

    AccelerationStructureBuildInput buildInput = {};
    buildInput.type = AccelerationStructureBuildInputType::Triangles;
    buildInput.triangles.vertexBuffers[0] = vertexBuffer;
    buildInput.triangles.vertexBufferCount = 1;
    buildInput.triangles.vertexFormat = Format::RGB32Float;
    buildInput.triangles.vertexCount = vertexCount;
    buildInput.triangles.vertexStride = sizeof(Vertex);
    buildInput.triangles.indexBuffer = indexBuffer;
    buildInput.triangles.indexFormat = IndexFormat::Uint32;
    buildInput.triangles.indexCount = indexCount;
    buildInput.triangles.flags = AccelerationStructureGeometryFlags::Opaque;

    AccelerationStructureBuildDesc buildDesc = {};
    buildDesc.inputs = &buildInput;
    buildDesc.inputCount = 1;

    AccelerationStructureSizes sizes;
    REQUIRE_CALL(device->getAccelerationStructureSizes(buildDesc, &sizes));

    BufferDesc scratchBufferDesc;
    scratchBufferDesc.usage = BufferUsage::UnorderedAccess;
    scratchBufferDesc.defaultState = ResourceState::UnorderedAccess;
    scratchBufferDesc.size = sizes.scratchSize;
    ComPtr<IBuffer> scratchBuffer = device->createBuffer(scratchBufferDesc);
    REQUIRE(scratchBuffer != nullptr);

    AccelerationStructureDesc blasDesc;
    blasDesc.kind = AccelerationStructureKind::BottomLevel;
    blasDesc.size = sizes.accelerationStructureSize;
    ComPtr<IAccelerationStructure> blas;
    REQUIRE_CALL(device->createAccelerationStructure(blasDesc, blas.writeRef()));

    auto commandEncoder = queue->createCommandEncoder();
    commandEncoder->buildAccelerationStructure(buildDesc, blas, nullptr, scratchBuffer, 0, nullptr);
    REQUIRE_CALL(queue->submit(commandEncoder->finish()));
    REQUIRE_CALL(queue->waitOnHost());

    TLAS tlas(device, queue, blas);

    const char* shaderSource = R"(
        [shader("compute")]
        [numthreads(2, 1, 1)]
        void computeMain(uint3 tid: SV_DispatchThreadID, RaytracingAccelerationStructure scene, RWStructuredBuffer<uint> result)
        {
            RayDesc ray;
            ray.Origin = float3(tid.x == 0 ? 0.25 : -0.75, 0.25, 0.0);
            ray.Direction = float3(0.0, 0.0, 1.0);
            ray.TMin = 0.0;
            ray.TMax = 10.0;
            RayQuery<RAY_FLAG_FORCE_OPAQUE> query;
            query.TraceRayInline(scene, RAY_FLAG_NONE, 0xff, ray);
            query.Proceed();
            result[tid.x] = query.CommittedStatus() == COMMITTED_TRIANGLE_HIT ? 1 : 0;
        }
    )";

    ComPtr<IShaderProgram> shaderProgram;
    REQUIRE_CALL(loadComputeProgramFromSource(device, shaderSource, shaderProgram.writeRef()));
    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

    BufferDesc resultBufferDesc = {};
    resultBufferDesc.size = 2 * sizeof(uint32_t);
    resultBufferDesc.elementSize = sizeof(uint32_t);
    resultBufferDesc.usage = BufferUsage::UnorderedAccess | BufferUsage::CopySource;
    resultBufferDesc.defaultState = ResourceState::UnorderedAccess;
    ComPtr<IBuffer> resultBuffer;
    REQUIRE_CALL(device->createBuffer(resultBufferDesc, nullptr, resultBuffer.writeRef()));

    commandEncoder = queue->createCommandEncoder();
    auto passEncoder = commandEncoder->beginComputePass();
    ShaderCursor cursor(passEncoder->bindPipeline(pipeline));
    cursor["scene"].setBinding(tlas.tlas);
    cursor["result"].setBinding(resultBuffer);
    passEncoder->dispatchCompute(1, 1, 1);
    passEncoder->end();
    REQUIRE_CALL(queue->submit(commandEncoder->finish()));
    REQUIRE_CALL(queue->waitOnHost());

    // The first triangle is hit, the second one (not covered by the index count) is not.
    compareComputeResult(device, resultBuffer, makeArray<uint32_t>(1u, 0u));
}
