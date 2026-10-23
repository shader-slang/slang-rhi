#include "test-ray-tracing-common.h"

using namespace rhi;
using namespace rhi::testing;

// A BLAS built with `preTransformBuffer` must apply the row-major 3x4 matrix to its vertices.
GPU_TEST_CASE("acceleration-structure-pre-transform", D3D12 | Vulkan | Metal)
{
    if (!device->hasFeature(Feature::RayQuery))
    {
        SKIP("Ray queries not supported");
    }

    auto queue = device->getQueue(QueueType::Graphics);

    // The triangle is at z = 1. The pre-transform moves it to z = 1.5 and doubles its size in x,
    // so a ray at x = 1.25 (outside the original triangle) hits it at distance 1.5.
    const Vertex vertices[] = {{0.f, 0.f, 1.f}, {1.f, 0.f, 1.f}, {0.f, 1.f, 1.f}};
    // clang-format off
    const float preTransform[12] = {
        2.f, 0.f, 0.f, 0.f,
        0.f, 1.f, 0.f, 0.f,
        0.f, 0.f, 1.f, 0.5f,
    };
    // clang-format on

    BufferDesc vertexDesc = {};
    vertexDesc.size = sizeof(vertices);
    vertexDesc.usage = BufferUsage::AccelerationStructureBuildInput;
    vertexDesc.defaultState = ResourceState::AccelerationStructureBuildInput;
    auto vertexBuffer = device->createBuffer(vertexDesc, vertices);
    REQUIRE(vertexBuffer);

    BufferDesc transformDesc = {};
    transformDesc.size = sizeof(preTransform);
    transformDesc.usage = BufferUsage::AccelerationStructureBuildInput;
    transformDesc.defaultState = ResourceState::AccelerationStructureBuildInput;
    auto transformBuffer = device->createBuffer(transformDesc, preTransform);
    REQUIRE(transformBuffer);

    AccelerationStructureBuildInput blasInput = {};
    blasInput.type = AccelerationStructureBuildInputType::Triangles;
    blasInput.triangles.vertexBuffers[0] = vertexBuffer;
    blasInput.triangles.vertexBufferCount = 1;
    blasInput.triangles.vertexFormat = Format::RGB32Float;
    blasInput.triangles.vertexCount = 3;
    blasInput.triangles.vertexStride = sizeof(Vertex);
    blasInput.triangles.preTransformBuffer = transformBuffer;
    blasInput.triangles.flags = AccelerationStructureGeometryFlags::Opaque;
    AccelerationStructureBuildDesc blasBuildDesc = {};
    blasBuildDesc.inputs = &blasInput;
    blasBuildDesc.inputCount = 1;

    AccelerationStructureSizes blasSizes = {};
    Result sizesResult = device->getAccelerationStructureSizes(blasBuildDesc, &blasSizes);
    // Metal reads a row-major pre-transform only on GPU family Apple9 with macOS 15 / iOS 18;
    // elsewhere the backend rejects the build instead of misplacing the geometry.
    if (device->getDeviceType() == DeviceType::Metal && sizesResult == SLANG_E_NOT_AVAILABLE)
    {
        SKIP("Metal device cannot read a row-major pre-transform");
    }
    REQUIRE_CALL(sizesResult);
    AccelerationStructureDesc blasDesc = {};
    blasDesc.kind = AccelerationStructureKind::BottomLevel;
    blasDesc.size = blasSizes.accelerationStructureSize;
    ComPtr<IAccelerationStructure> blas;
    REQUIRE_CALL(device->createAccelerationStructure(blasDesc, blas.writeRef()));

    BufferDesc blasScratchDesc = {};
    blasScratchDesc.size = blasSizes.scratchSize;
    blasScratchDesc.usage = BufferUsage::UnorderedAccess;
    blasScratchDesc.defaultState = ResourceState::UnorderedAccess;
    auto blasScratch = device->createBuffer(blasScratchDesc);
    REQUIRE(blasScratch);
    {
        auto encoder = queue->createCommandEncoder();
        encoder->buildAccelerationStructure(blasBuildDesc, blas, nullptr, blasScratch, 0, nullptr);
        REQUIRE_CALL(queue->submit(encoder->finish()));
        REQUIRE_CALL(queue->waitOnHost());
    }

    TLAS tlas(device, queue, blas);

    ComPtr<IShaderProgram> program;
    REQUIRE_CALL(loadComputeProgramFromSource(
        device,
        R"(
        RaytracingAccelerationStructure scene;
        RWStructuredBuffer<float> results;
        [shader("compute")]
        [numthreads(1, 1, 1)]
        void computeMain(uint3 tid : SV_DispatchThreadID)
        {
            RayDesc ray;
            ray.Origin = float3(tid.x == 0 ? 1.25 : 0.25, 0.25, 0.0);
            ray.Direction = float3(0.0, 0.0, 1.0);
            ray.TMin = 0.0;
            ray.TMax = 10.0;
            RayQuery<RAY_FLAG_NONE> query;
            query.TraceRayInline(scene, RAY_FLAG_NONE, 0xff, ray);
            while (query.Proceed()) {}
            results[tid.x] = query.CommittedStatus() == COMMITTED_TRIANGLE_HIT
                ? query.CommittedRayT() : -1.0;
        }
    )",
        program.writeRef()
    ));

    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = program;
    auto pipeline = device->createComputePipeline(pipelineDesc);
    REQUIRE(pipeline);

    BufferDesc resultDesc = {};
    resultDesc.size = 2 * sizeof(float);
    resultDesc.elementSize = sizeof(float);
    resultDesc.usage = BufferUsage::UnorderedAccess | BufferUsage::CopySource;
    resultDesc.defaultState = ResourceState::UnorderedAccess;
    auto results = device->createBuffer(resultDesc);
    REQUIRE(results);

    auto encoder = queue->createCommandEncoder();
    auto pass = encoder->beginComputePass();
    auto root = pass->bindPipeline(pipeline);
    ShaderCursor cursor(root);
    REQUIRE_CALL(cursor["scene"].setBinding(tlas.tlas));
    REQUIRE_CALL(cursor["results"].setBinding(results));
    pass->dispatchCompute(2, 1, 1);
    pass->end();
    REQUIRE_CALL(queue->submit(encoder->finish()));
    REQUIRE_CALL(queue->waitOnHost());

    compareComputeResult(device, results, makeArray<float>(1.5f, 1.5f));
}
