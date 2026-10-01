#include "test-ray-tracing-common.h"

using namespace rhi;
using namespace rhi::testing;

static ComPtr<IAccelerationStructure> buildTriangle(IDevice* device, ICommandQueue* queue, float z, float xOffset = 0.f)
{
    const Vertex vertices[] = {{xOffset, 0.f, z}, {xOffset + 1.f, 0.f, z}, {xOffset, 1.f, z}};
    BufferDesc vertexDesc = {};
    vertexDesc.size = sizeof(vertices);
    vertexDesc.usage = BufferUsage::AccelerationStructureBuildInput;
    vertexDesc.defaultState = ResourceState::AccelerationStructureBuildInput;
    auto vertexBuffer = device->createBuffer(vertexDesc, vertices);
    REQUIRE(vertexBuffer);

    AccelerationStructureBuildInput input = {};
    input.type = AccelerationStructureBuildInputType::Triangles;
    input.triangles.vertexBuffers[0] = vertexBuffer;
    input.triangles.vertexBufferCount = 1;
    input.triangles.vertexFormat = Format::RGB32Float;
    input.triangles.vertexCount = 3;
    input.triangles.vertexStride = sizeof(Vertex);
    input.triangles.flags = AccelerationStructureGeometryFlags::Opaque;
    AccelerationStructureBuildDesc buildDesc = {};
    buildDesc.inputs = &input;
    buildDesc.inputCount = 1;
    AccelerationStructureSizes sizes = {};
    REQUIRE_CALL(device->getAccelerationStructureSizes(buildDesc, &sizes));

    AccelerationStructureDesc desc = {};
    desc.kind = AccelerationStructureKind::BottomLevel;
    desc.size = sizes.accelerationStructureSize;
    ComPtr<IAccelerationStructure> structure;
    REQUIRE_CALL(device->createAccelerationStructure(desc, structure.writeRef()));
    REQUIRE(structure);
    BufferDesc scratchDesc = {};
    scratchDesc.size = sizes.scratchSize;
    scratchDesc.usage = BufferUsage::UnorderedAccess;
    scratchDesc.defaultState = ResourceState::UnorderedAccess;
    auto scratch = device->createBuffer(scratchDesc);
    REQUIRE(scratch);
    {
        auto encoder = queue->createCommandEncoder();
        encoder->buildAccelerationStructure(buildDesc, structure, nullptr, scratch, 0, nullptr);
        REQUIRE_CALL(queue->submit(encoder->finish()));
        REQUIRE_CALL(queue->waitOnHost());
    }
    return structure;
}

static void buildScene(
    IDevice* device,
    ICommandQueue* queue,
    IAccelerationStructure* blas,
    float zOffset,
    ComPtr<IAccelerationStructure>& scene
)
{
    AccelerationStructureInstanceDescGeneric instance = {};
    instance.transform[0][0] = instance.transform[1][1] = instance.transform[2][2] = 1.f;
    instance.transform[2][3] = zOffset;
    instance.instanceMask = 0xff;
    instance.accelerationStructure = blas->getHandle();
    auto instanceType = getAccelerationStructureInstanceDescType(device);
    Size instanceStride = getAccelerationStructureInstanceDescSize(instanceType);
    std::vector<uint8_t> nativeInstance(instanceStride);
    convertAccelerationStructureInstanceDesc(instanceType, nativeInstance.data(), &instance);
    BufferDesc instanceDesc = {};
    instanceDesc.size = instanceStride;
    instanceDesc.usage = BufferUsage::AccelerationStructureBuildInput;
    instanceDesc.defaultState = ResourceState::AccelerationStructureBuildInput;
    auto instanceBuffer = device->createBuffer(instanceDesc, nativeInstance.data());
    REQUIRE(instanceBuffer);

    AccelerationStructureBuildInput input = {};
    input.type = AccelerationStructureBuildInputType::Instances;
    input.instances.instanceBuffer = instanceBuffer;
    input.instances.instanceCount = 1;
    input.instances.instanceStride = instanceStride;
    AccelerationStructureBuildDesc buildDesc = {};
    buildDesc.inputs = &input;
    buildDesc.inputCount = 1;
    buildDesc.flags = AccelerationStructureBuildFlags::AllowUpdate;
    buildDesc.mode = scene ? AccelerationStructureBuildMode::Update : AccelerationStructureBuildMode::Build;
    AccelerationStructureSizes sizes = {};
    REQUIRE_CALL(device->getAccelerationStructureSizes(buildDesc, &sizes));
    if (!scene)
    {
        AccelerationStructureDesc desc = {};
        desc.kind = AccelerationStructureKind::TopLevel;
        desc.size = sizes.accelerationStructureSize;
        REQUIRE_CALL(device->createAccelerationStructure(desc, scene.writeRef()));
    }
    BufferDesc scratchDesc = {};
    scratchDesc.size = std::max(sizes.scratchSize, sizes.updateScratchSize);
    scratchDesc.usage = BufferUsage::UnorderedAccess;
    scratchDesc.defaultState = ResourceState::UnorderedAccess;
    auto scratch = device->createBuffer(scratchDesc);
    REQUIRE(scratch);
    auto encoder = queue->createCommandEncoder();
    auto source = buildDesc.mode == AccelerationStructureBuildMode::Update ? scene.get() : nullptr;
    encoder->buildAccelerationStructure(buildDesc, scene, source, scratch, 0, nullptr);
    REQUIRE_CALL(queue->submit(encoder->finish()));
    REQUIRE_CALL(queue->waitOnHost());
}

GPU_TEST_CASE("acceleration-structure-release-and-rebuild", D3D12 | Vulkan | Metal | DontCacheDevice)
{
    if (!device->hasFeature(Feature::RayQuery))
        SKIP("Ray queries not supported");

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
            ray.Origin = float3(tid.x == 0 ? 0.25 : 2.25, 0.25, 0.0);
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

    auto queue = device->getQueue(QueueType::Graphics);
    for (int iteration = 0; iteration < 8; ++iteration)
    {
        // Keep an unrelated BLAS alive before the target. Compacting the registry
        // would change the target's index and could select the wrong geometry.
        auto discarded = buildTriangle(device, queue, 3.f);
        // The second ray hits only the decoy's geometry. It must still miss the
        // TLAS: the native array is an index table, not a list of instances.
        auto decoy = buildTriangle(device, queue, 1.f, 2.f);
        auto target = buildTriangle(device, queue, 2.f);

        // Release more slots than the new TLAS can reuse, leaving holes when
        // the backend constructs its native instance acceleration-structure array.
        {
            auto temporary0 = buildTriangle(device, queue, 4.f);
            auto temporary1 = buildTriangle(device, queue, 5.f);
            // Retire the earlier slot first, so the TLAS reuses a later slot and
            // a hole remains before the live decoy and target.
            discarded.setNull();
            REQUIRE_CALL(queue->waitOnHost());
        }
        REQUIRE_CALL(queue->waitOnHost());

        ComPtr<IAccelerationStructure> scene;
        buildScene(device, queue, target, 0.f, scene);
        auto trace = [&](float expectedDistance)
        {
            auto encoder = queue->createCommandEncoder();
            auto pass = encoder->beginComputePass();
            auto root = pass->bindPipeline(pipeline);
            ShaderCursor cursor(root);
            REQUIRE_CALL(cursor["scene"].setBinding(scene));
            REQUIRE_CALL(cursor["results"].setBinding(results));
            pass->dispatchCompute(2, 1, 1);
            pass->end();
            REQUIRE_CALL(queue->submit(encoder->finish()));
            REQUIRE_CALL(queue->waitOnHost());
            compareComputeResult(device, results, makeArray<float>(expectedDistance, -1.f));
        };
        trace(2.f);
        // Refit uses the same registry snapshot, including its dummy-filled holes.
        // Updating the instance transform must move the hit and keep the decoy absent.
        buildScene(device, queue, target, 1.f, scene);
        trace(3.f);
        // All structures are released here. The next iteration replaces the scene.
    }
    REQUIRE_CALL(queue->waitOnHost());
}
