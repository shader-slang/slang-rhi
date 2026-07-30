#include "testing.h"

#include "cpu/cpu-acceleration-structure.h"

#include <limits>

using namespace rhi;
using namespace rhi::testing;

namespace {

using TestRayDesc = slang_prelude::RayDesc;

ComPtr<IBuffer> createBuildBuffer(IDevice* device, const void* data, size_t size)
{
    BufferDesc desc = {};
    desc.size = size;
    desc.usage = BufferUsage::AccelerationStructureBuildInput;
    desc.defaultState = ResourceState::AccelerationStructureBuildInput;
    ComPtr<IBuffer> buffer = device->createBuffer(desc, data);
    REQUIRE(buffer);
    return buffer;
}

ComPtr<IAccelerationStructure> buildCompactedAccelerationStructure(
    IDevice* device,
    ICommandQueue* queue,
    const AccelerationStructureBuildDesc& buildDesc,
    AccelerationStructureKind kind
)
{
    AccelerationStructureBuildDesc compactedBuildDesc = buildDesc;
    compactedBuildDesc.flags |= AccelerationStructureBuildFlags::AllowCompaction;

    AccelerationStructureSizes sizes = {};
    REQUIRE_CALL(device->getAccelerationStructureSizes(compactedBuildDesc, &sizes));

    BufferDesc scratchDesc = {};
    scratchDesc.size = sizes.scratchSize;
    scratchDesc.usage = BufferUsage::UnorderedAccess;
    scratchDesc.defaultState = ResourceState::UnorderedAccess;
    ComPtr<IBuffer> scratch = device->createBuffer(scratchDesc);
    REQUIRE(scratch);

    QueryPoolDesc queryPoolDesc = {};
    queryPoolDesc.count = 1;
    queryPoolDesc.type = QueryType::AccelerationStructureCompactedSize;
    ComPtr<IQueryPool> compactedSizeQuery;
    REQUIRE_CALL(device->createQueryPool(queryPoolDesc, compactedSizeQuery.writeRef()));
    REQUIRE_CALL(compactedSizeQuery->reset());

    AccelerationStructureDesc draftDesc = {};
    draftDesc.kind = kind;
    draftDesc.size = sizes.accelerationStructureSize;
    ComPtr<IAccelerationStructure> draft;
    REQUIRE_CALL(device->createAccelerationStructure(draftDesc, draft.writeRef()));

    AccelerationStructureQueryDesc compactedSizeQueryDesc = {};
    compactedSizeQueryDesc.queryType = QueryType::AccelerationStructureCompactedSize;
    compactedSizeQueryDesc.queryPool = compactedSizeQuery;

    ComPtr<ICommandEncoder> encoder = queue->createCommandEncoder();
    encoder->buildAccelerationStructure(compactedBuildDesc, draft, nullptr, scratch, 1, &compactedSizeQueryDesc);
    REQUIRE_CALL(queue->submit(encoder->finish()));
    REQUIRE_CALL(queue->waitOnHost());

    uint64_t compactedSize = 0;
    REQUIRE_CALL(compactedSizeQuery->getResult(0, 1, &compactedSize));
    CHECK(compactedSize == sizes.accelerationStructureSize);

    AccelerationStructureDesc compactedDesc = {};
    compactedDesc.kind = kind;
    compactedDesc.size = compactedSize;
    ComPtr<IAccelerationStructure> compacted;
    REQUIRE_CALL(device->createAccelerationStructure(compactedDesc, compacted.writeRef()));

    encoder = queue->createCommandEncoder();
    encoder->copyAccelerationStructure(compacted, draft, AccelerationStructureCopyMode::Compact);
    REQUIRE_CALL(queue->submit(encoder->finish()));
    REQUIRE_CALL(queue->waitOnHost());

    // The compacted object must retain the hierarchy independently of the source object's
    // lifetime, just like a native backend copy.
    draft.setNull();
    return compacted;
}

slang_prelude::RaytracingAccelerationStructure getShaderHandle(IAccelerationStructure* accelerationStructure)
{
    cpu::AccelerationStructureImpl* implementation =
        checked_cast<cpu::AccelerationStructureImpl*>(accelerationStructure);
    return {
        static_cast<slang_prelude::IRaytracingAccelerationStructure*>(implementation),
    };
}

} // namespace

GPU_TEST_CASE("cpu-ray-query-triangle-state-machine", CPU)
{
    SLANG_UNUSED(ctx);
    REQUIRE(device->hasFeature(Feature::RayQuery));
    ComPtr<ICommandQueue> queue = device->getQueue(QueueType::Graphics);
    REQUIRE(queue);

    const float indexedVertices[][3] = {
        {-10.0f, -10.0f, -10.0f},
        {0.0f, 0.0f, 1.0f},
        {1.0f, 0.0f, 1.0f},
        {0.0f, 1.0f, 1.0f},
    };
    const uint16_t indices[] = {1, 2, 3};
    const float nonIndexedVertices[][3] = {
        {0.0f, 0.0f, 2.0f},
        {1.0f, 0.0f, 2.0f},
        {0.0f, 1.0f, 2.0f},
    };
    ComPtr<IBuffer> indexedVertexBuffer = createBuildBuffer(device, indexedVertices, sizeof(indexedVertices));
    ComPtr<IBuffer> indexBuffer = createBuildBuffer(device, indices, sizeof(indices));
    ComPtr<IBuffer> nonIndexedVertexBuffer = createBuildBuffer(device, nonIndexedVertices, sizeof(nonIndexedVertices));

    AccelerationStructureBuildInput geometryInputs[2] = {};
    geometryInputs[0].type = AccelerationStructureBuildInputType::Triangles;
    geometryInputs[0].triangles.vertexBuffers[0] = indexedVertexBuffer;
    geometryInputs[0].triangles.vertexBufferCount = 1;
    geometryInputs[0].triangles.vertexFormat = Format::RGB32Float;
    geometryInputs[0].triangles.vertexCount = 4;
    geometryInputs[0].triangles.vertexStride = sizeof(indexedVertices[0]);
    geometryInputs[0].triangles.indexBuffer = indexBuffer;
    geometryInputs[0].triangles.indexFormat = IndexFormat::Uint16;
    geometryInputs[0].triangles.indexCount = 3;
    geometryInputs[0].triangles.flags = AccelerationStructureGeometryFlags::None;

    geometryInputs[1].type = AccelerationStructureBuildInputType::Triangles;
    geometryInputs[1].triangles.vertexBuffers[0] = nonIndexedVertexBuffer;
    geometryInputs[1].triangles.vertexBufferCount = 1;
    geometryInputs[1].triangles.vertexFormat = Format::RGB32Float;
    geometryInputs[1].triangles.vertexCount = 3;
    geometryInputs[1].triangles.vertexStride = sizeof(nonIndexedVertices[0]);
    geometryInputs[1].triangles.flags = AccelerationStructureGeometryFlags::Opaque;

    AccelerationStructureBuildDesc bottomLevelBuildDesc = {};
    bottomLevelBuildDesc.inputs = geometryInputs;
    bottomLevelBuildDesc.inputCount = 2;
    ComPtr<IAccelerationStructure> bottomLevel = buildCompactedAccelerationStructure(
        device,
        queue,
        bottomLevelBuildDesc,
        AccelerationStructureKind::BottomLevel
    );

    AccelerationStructureInstanceDescGeneric instanceDescs[3] = {};
    const float identityTransform[3][4] = {
        {1.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f, 0.0f},
    };
    std::memcpy(instanceDescs[0].transform, identityTransform, sizeof(identityTransform));
    instanceDescs[0].instanceID = 7;
    instanceDescs[0].instanceMask = 0x1;
    instanceDescs[0].instanceContributionToHitGroupIndex = 11;
    instanceDescs[0].accelerationStructure = bottomLevel->getHandle();

    std::memcpy(instanceDescs[1].transform, identityTransform, sizeof(identityTransform));
    instanceDescs[1].transform[2][3] = 3.0f;
    instanceDescs[1].instanceID = 8;
    instanceDescs[1].instanceMask = 0x2;
    instanceDescs[1].instanceContributionToHitGroupIndex = 12;
    instanceDescs[1].flags = AccelerationStructureInstanceFlags::TriangleFrontCounterClockwise;
    instanceDescs[1].accelerationStructure = bottomLevel->getHandle();

    // This instance rotates 90 degrees around Z, scales X and Y non-uniformly, and translates
    // along both X and Z.
    const float rotatedScaledTransform[3][4] = {
        {0.0f, -2.0f, 0.0f, 1.0f},
        {0.5f, 0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f, 6.0f},
    };
    std::memcpy(instanceDescs[2].transform, rotatedScaledTransform, sizeof(rotatedScaledTransform));
    instanceDescs[2].instanceID = 9;
    instanceDescs[2].instanceMask = 0x4;
    instanceDescs[2].instanceContributionToHitGroupIndex = 13;
    instanceDescs[2].flags = AccelerationStructureInstanceFlags::ForceOpaque;
    instanceDescs[2].accelerationStructure = bottomLevel->getHandle();

    ComPtr<IBuffer> instanceBuffer = createBuildBuffer(device, instanceDescs, sizeof(instanceDescs));
    AccelerationStructureBuildInput instanceInput = {};
    instanceInput.type = AccelerationStructureBuildInputType::Instances;
    instanceInput.instances.instanceBuffer = instanceBuffer;
    instanceInput.instances.instanceStride = sizeof(instanceDescs[0]);
    instanceInput.instances.instanceCount = SLANG_COUNT_OF(instanceDescs);
    AccelerationStructureBuildDesc topLevelBuildDesc = {};
    topLevelBuildDesc.inputs = &instanceInput;
    topLevelBuildDesc.inputCount = 1;
    ComPtr<IAccelerationStructure> topLevel =
        buildCompactedAccelerationStructure(device, queue, topLevelBuildDesc, AccelerationStructureKind::TopLevel);

    const slang_prelude::RaytracingAccelerationStructure shaderHandle = getShaderHandle(topLevel);
    const TestRayDesc ray = {
        {0.25f, 0.25f, 0.0f},
        0.0f,
        {0.0f, 0.0f, 1.0f},
        100.0f,
    };

    // The non-opaque indexed geometry pauses traversal. Committing it replaces any farther
    // opaque hit that traversal may already have encountered.
    slang_prelude::RayQuery<0> query;
    query.TraceRayInline(shaderHandle, 0, 0x1, ray);
    REQUIRE(query.Proceed());
    CHECK(query.CandidateType() == slang_prelude::SLANG_RAY_QUERY_CANDIDATE_NON_OPAQUE_TRIANGLE);
    CHECK(query.CandidateTriangleRayT() == doctest::Approx(1.0f));
    CHECK(query.CandidateGeometryIndex() == 0);
    CHECK(query.CandidatePrimitiveIndex() == 0);
    CHECK(query.CandidateInstanceIndex() == 0);
    CHECK(query.CandidateInstanceID() == 7);
    CHECK(query.CandidateInstanceContributionToHitGroupIndex() == 11);
    CHECK_FALSE(query.CandidateTriangleFrontFace());
    CHECK(query.RayFlags() == 0);
    CHECK(query.WorldRayOrigin().x == doctest::Approx(0.25f));
    CHECK(query.WorldRayDirection().z == doctest::Approx(1.0f));
    CHECK(query.RayTMin() == doctest::Approx(0.0f));
    CHECK(query.CandidateTriangleBarycentrics().x == doctest::Approx(0.25f));
    CHECK(query.CandidateTriangleBarycentrics().y == doctest::Approx(0.25f));
    query.CommitNonOpaqueTriangleHit();
    CHECK_FALSE(query.Proceed());
    CHECK_FALSE(query.Proceed());
    CHECK(query.CommittedStatus() == slang_prelude::SLANG_RAY_QUERY_COMMITTED_TRIANGLE_HIT);
    CHECK(query.CommittedRayT() == doctest::Approx(1.0f));
    CHECK(query.CommittedGeometryIndex() == 0);

    // Forcing every triangle non-opaque yields both geometries as separate resumable candidates.
    slang_prelude::RayQuery<0> nonOpaqueQuery;
    nonOpaqueQuery.TraceRayInline(shaderHandle, slang_prelude::SLANG_RAY_QUERY_FLAG_FORCE_NON_OPAQUE, 0x1, ray);
    uint32_t candidateGeometryMask = 0;
    uint32_t candidateCount = 0;
    while (nonOpaqueQuery.Proceed())
    {
        candidateGeometryMask |= 1u << nonOpaqueQuery.CandidateGeometryIndex();
        ++candidateCount;
    }
    CHECK(candidateCount == 2);
    CHECK(candidateGeometryMask == 0x3);
    CHECK(nonOpaqueQuery.CommittedStatus() == slang_prelude::SLANG_RAY_QUERY_COMMITTED_NOTHING);

    // The second instance uses a translated transform and reverses the default facing convention.
    slang_prelude::RayQuery<0> transformedQuery;
    transformedQuery.TraceRayInline(shaderHandle, slang_prelude::SLANG_RAY_QUERY_FLAG_FORCE_OPAQUE, 0x2, ray);
    CHECK_FALSE(transformedQuery.Proceed());
    CHECK(transformedQuery.CommittedRayT() == doctest::Approx(4.0f));
    CHECK(transformedQuery.CommittedInstanceIndex() == 1);
    CHECK(transformedQuery.CommittedInstanceID() == 8);
    CHECK(transformedQuery.CommittedInstanceContributionToHitGroupIndex() == 12);
    CHECK(transformedQuery.CommittedGeometryIndex() == 0);
    CHECK(transformedQuery.CommittedTriangleFrontFace());
    CHECK(transformedQuery.CommittedObjectRayOrigin().z == doctest::Approx(-3.0f));
    CHECK(transformedQuery.CommittedObjectToWorld3x4().rows[2][3] == doctest::Approx(3.0f));
    CHECK(transformedQuery.CommittedWorldToObject3x4().rows[2][3] == doctest::Approx(-3.0f));

    // The ray parameter remains unchanged across affine instance transforms. In particular, the
    // direction is not normalized even when the instance has non-uniform scale.
    const TestRayDesc unnormalizedRay = {
        {0.25f, 0.125f, 0.0f},
        0.0f,
        {0.0f, 0.0f, 2.0f},
        100.0f,
    };
    slang_prelude::RayQuery<0> rotatedScaledQuery;
    rotatedScaledQuery.TraceRayInline(shaderHandle, 0, 0x4, unnormalizedRay);
    CHECK_FALSE(rotatedScaledQuery.Proceed());
    CHECK(rotatedScaledQuery.CommittedRayT() == doctest::Approx(3.5f));
    CHECK(rotatedScaledQuery.CommittedInstanceIndex() == 2);
    CHECK(rotatedScaledQuery.CommittedInstanceID() == 9);
    CHECK(rotatedScaledQuery.CommittedInstanceContributionToHitGroupIndex() == 13);
    CHECK(rotatedScaledQuery.CommittedObjectRayOrigin().x == doctest::Approx(0.25f));
    CHECK(rotatedScaledQuery.CommittedObjectRayOrigin().y == doctest::Approx(0.375f));
    CHECK(rotatedScaledQuery.CommittedObjectRayOrigin().z == doctest::Approx(-6.0f));
    CHECK(rotatedScaledQuery.CommittedObjectRayDirection().z == doctest::Approx(2.0f));

    slang_prelude::RayQuery<0> maskedQuery;
    maskedQuery.TraceRayInline(shaderHandle, 0, 0x8, ray);
    CHECK_FALSE(maskedQuery.Proceed());
    CHECK(maskedQuery.CommittedStatus() == slang_prelude::SLANG_RAY_QUERY_COMMITTED_NOTHING);

    const TestRayDesc missRay = {
        {2.0f, 2.0f, 0.0f},
        0.0f,
        {0.0f, 0.0f, 1.0f},
        100.0f,
    };
    slang_prelude::RayQuery<0> missQuery;
    missQuery.TraceRayInline(shaderHandle, 0, 0x1, missRay);
    CHECK_FALSE(missQuery.Proceed());
    CHECK(missQuery.CommittedStatus() == slang_prelude::SLANG_RAY_QUERY_COMMITTED_NOTHING);

    const TestRayDesc tMinBoundaryRay = {
        {0.25f, 0.25f, 0.0f},
        1.0f,
        {0.0f, 0.0f, 1.0f},
        100.0f,
    };
    slang_prelude::RayQuery<0> tMinBoundaryQuery;
    tMinBoundaryQuery
        .TraceRayInline(shaderHandle, slang_prelude::SLANG_RAY_QUERY_FLAG_FORCE_OPAQUE, 0x1, tMinBoundaryRay);
    CHECK_FALSE(tMinBoundaryQuery.Proceed());
    CHECK(tMinBoundaryQuery.CommittedRayT() == doctest::Approx(2.0f));

    const TestRayDesc tMaxBoundaryRay = {
        {0.25f, 0.25f, 0.0f},
        0.0f,
        {0.0f, 0.0f, 1.0f},
        1.0f,
    };
    slang_prelude::RayQuery<0> tMaxBoundaryQuery;
    tMaxBoundaryQuery
        .TraceRayInline(shaderHandle, slang_prelude::SLANG_RAY_QUERY_FLAG_FORCE_OPAQUE, 0x1, tMaxBoundaryRay);
    CHECK_FALSE(tMaxBoundaryQuery.Proceed());
    CHECK(tMaxBoundaryQuery.CommittedStatus() == slang_prelude::SLANG_RAY_QUERY_COMMITTED_NOTHING);

    slang_prelude::RayQuery<0> culledQuery;
    culledQuery.TraceRayInline(shaderHandle, slang_prelude::SLANG_RAY_QUERY_FLAG_CULL_BACK_FACING_TRIANGLES, 0x1, ray);
    CHECK_FALSE(culledQuery.Proceed());
    CHECK(culledQuery.CommittedStatus() == slang_prelude::SLANG_RAY_QUERY_COMMITTED_NOTHING);

    slang_prelude::RayQuery<0> frontCulledQuery;
    frontCulledQuery.TraceRayInline(
        shaderHandle,
        slang_prelude::SLANG_RAY_QUERY_FLAG_FORCE_OPAQUE |
            slang_prelude::SLANG_RAY_QUERY_FLAG_CULL_FRONT_FACING_TRIANGLES,
        0x2,
        ray
    );
    CHECK_FALSE(frontCulledQuery.Proceed());
    CHECK(frontCulledQuery.CommittedStatus() == slang_prelude::SLANG_RAY_QUERY_COMMITTED_NOTHING);

    slang_prelude::RayQuery<0> skipTrianglesQuery;
    skipTrianglesQuery.TraceRayInline(shaderHandle, slang_prelude::SLANG_RAY_QUERY_FLAG_SKIP_TRIANGLES, 0x1, ray);
    CHECK_FALSE(skipTrianglesQuery.Proceed());
    CHECK(skipTrianglesQuery.CommittedStatus() == slang_prelude::SLANG_RAY_QUERY_COMMITTED_NOTHING);

    slang_prelude::RayQuery<slang_prelude::SLANG_RAY_QUERY_FLAG_CULL_BACK_FACING_TRIANGLES> genericFlagQuery;
    genericFlagQuery.TraceRayInline(shaderHandle, 0, 0x1, ray);
    CHECK_FALSE(genericFlagQuery.Proceed());
    CHECK(genericFlagQuery.RayFlags() == slang_prelude::SLANG_RAY_QUERY_FLAG_CULL_BACK_FACING_TRIANGLES);

    slang_prelude::RayQuery<0> cullOpaqueQuery;
    cullOpaqueQuery.TraceRayInline(shaderHandle, slang_prelude::SLANG_RAY_QUERY_FLAG_CULL_OPAQUE, 0x1, ray);
    REQUIRE(cullOpaqueQuery.Proceed());
    CHECK(cullOpaqueQuery.CandidateGeometryIndex() == 0);
    CHECK_FALSE(cullOpaqueQuery.Proceed());
    CHECK(cullOpaqueQuery.CommittedStatus() == slang_prelude::SLANG_RAY_QUERY_COMMITTED_NOTHING);

    slang_prelude::RayQuery<0> cullNonOpaqueQuery;
    cullNonOpaqueQuery.TraceRayInline(shaderHandle, slang_prelude::SLANG_RAY_QUERY_FLAG_CULL_NON_OPAQUE, 0x1, ray);
    CHECK_FALSE(cullNonOpaqueQuery.Proceed());
    CHECK(cullNonOpaqueQuery.CommittedRayT() == doctest::Approx(2.0f));
    CHECK(cullNonOpaqueQuery.CommittedGeometryIndex() == 1);

    slang_prelude::RayQuery<0> acceptFirstQuery;
    acceptFirstQuery.TraceRayInline(
        shaderHandle,
        slang_prelude::SLANG_RAY_QUERY_FLAG_FORCE_OPAQUE |
            slang_prelude::SLANG_RAY_QUERY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH,
        0x1,
        ray
    );
    CHECK_FALSE(acceptFirstQuery.Proceed());
    CHECK(acceptFirstQuery.CommittedStatus() == slang_prelude::SLANG_RAY_QUERY_COMMITTED_TRIANGLE_HIT);
    CHECK_FALSE(acceptFirstQuery.Proceed());

    slang_prelude::RayQuery<0> abortedQuery;
    abortedQuery.TraceRayInline(shaderHandle, slang_prelude::SLANG_RAY_QUERY_FLAG_FORCE_NON_OPAQUE, 0x1, ray);
    REQUIRE(abortedQuery.Proceed());
    abortedQuery.Abort();
    CHECK_FALSE(abortedQuery.Proceed());
    CHECK(abortedQuery.CommittedStatus() == slang_prelude::SLANG_RAY_QUERY_COMMITTED_NOTHING);

    AccelerationStructureBuildInput unsupportedInput = {};
    unsupportedInput.type = AccelerationStructureBuildInputType::Spheres;
    unsupportedInput.spheres.vertexPositionFormat = Format::RGB32Float;
    unsupportedInput.spheres.vertexRadiusFormat = Format::R32Float;
    AccelerationStructureBuildDesc unsupportedBuildDesc = {};
    unsupportedBuildDesc.inputs = &unsupportedInput;
    unsupportedBuildDesc.inputCount = 1;
    AccelerationStructureSizes unsupportedSizes = {};
    CHECK(device->getAccelerationStructureSizes(unsupportedBuildDesc, &unsupportedSizes) == SLANG_E_NOT_AVAILABLE);
}

GPU_TEST_CASE("cpu-ray-query-procedural-state-machine", CPU)
{
    SLANG_UNUSED(ctx);
    REQUIRE(device->hasFeature(Feature::RayQuery));
    ComPtr<ICommandQueue> queue = device->getQueue(QueueType::Graphics);
    REQUIRE(queue);

    struct PaddedAABB
    {
        AccelerationStructureAABB bounds;
        uint32_t padding[2];
    };
    const PaddedAABB geometry0AABBs[] = {
        {{-0.5f, -0.5f, 1.0f, 0.5f, 0.5f, 2.0f}, {}},
        {{2.0f, 2.0f, 1.0f, 3.0f, 3.0f, 2.0f}, {}},
    };
    const AccelerationStructureAABB geometry1AABBs[] = {
        {-0.5f, -0.5f, 4.0f, 0.5f, 0.5f, 5.0f},
    };
    ComPtr<IBuffer> geometry0Buffer = createBuildBuffer(device, geometry0AABBs, sizeof(geometry0AABBs));
    ComPtr<IBuffer> geometry1Buffer = createBuildBuffer(device, geometry1AABBs, sizeof(geometry1AABBs));

    AccelerationStructureBuildInput geometryInputs[2] = {};
    geometryInputs[0].type = AccelerationStructureBuildInputType::ProceduralPrimitives;
    geometryInputs[0].proceduralPrimitives.aabbBuffers[0] = geometry0Buffer;
    geometryInputs[0].proceduralPrimitives.aabbBufferCount = 1;
    geometryInputs[0].proceduralPrimitives.aabbStride = sizeof(PaddedAABB);
    geometryInputs[0].proceduralPrimitives.primitiveCount = 2;

    geometryInputs[1].type = AccelerationStructureBuildInputType::ProceduralPrimitives;
    geometryInputs[1].proceduralPrimitives.aabbBuffers[0] = geometry1Buffer;
    geometryInputs[1].proceduralPrimitives.aabbBufferCount = 1;
    geometryInputs[1].proceduralPrimitives.aabbStride = sizeof(AccelerationStructureAABB);
    geometryInputs[1].proceduralPrimitives.primitiveCount = 1;
    geometryInputs[1].proceduralPrimitives.flags = AccelerationStructureGeometryFlags::Opaque;

    AccelerationStructureBuildDesc bottomLevelBuildDesc = {};
    bottomLevelBuildDesc.inputs = geometryInputs;
    bottomLevelBuildDesc.inputCount = SLANG_COUNT_OF(geometryInputs);
    ComPtr<IAccelerationStructure> bottomLevel = buildCompactedAccelerationStructure(
        device,
        queue,
        bottomLevelBuildDesc,
        AccelerationStructureKind::BottomLevel
    );

    AccelerationStructureInstanceDescGeneric instanceDesc = {};
    const float identityTransform[3][4] = {
        {1.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f, 0.0f},
    };
    std::memcpy(instanceDesc.transform, identityTransform, sizeof(identityTransform));
    instanceDesc.instanceID = 42;
    instanceDesc.instanceMask = 0x1;
    instanceDesc.instanceContributionToHitGroupIndex = 9;
    instanceDesc.accelerationStructure = bottomLevel->getHandle();

    ComPtr<IBuffer> instanceBuffer = createBuildBuffer(device, &instanceDesc, sizeof(instanceDesc));
    AccelerationStructureBuildInput instanceInput = {};
    instanceInput.type = AccelerationStructureBuildInputType::Instances;
    instanceInput.instances.instanceBuffer = instanceBuffer;
    instanceInput.instances.instanceStride = sizeof(instanceDesc);
    instanceInput.instances.instanceCount = 1;
    AccelerationStructureBuildDesc topLevelBuildDesc = {};
    topLevelBuildDesc.inputs = &instanceInput;
    topLevelBuildDesc.inputCount = 1;
    ComPtr<IAccelerationStructure> topLevel =
        buildCompactedAccelerationStructure(device, queue, topLevelBuildDesc, AccelerationStructureKind::TopLevel);

    const slang_prelude::RaytracingAccelerationStructure shaderHandle = getShaderHandle(topLevel);
    const TestRayDesc ray = {
        {0.0f, 0.0f, 0.0f},
        0.0f,
        {0.0f, 0.0f, 1.0f},
        10.0f,
    };

    // A procedural BLAS yields only the individual AABBs intersected by the ray. The second
    // primitive in geometry 0 is outside the ray even though it may share a TinyBVH leaf.
    slang_prelude::RayQuery<0> enumerationQuery;
    enumerationQuery.TraceRayInline(shaderHandle, slang_prelude::SLANG_RAY_QUERY_FLAG_SKIP_TRIANGLES, 0x1, ray);
    uint32_t geometryMask = 0;
    uint32_t candidateCount = 0;
    while (enumerationQuery.Proceed())
    {
        CHECK(enumerationQuery.CandidateType() == slang_prelude::SLANG_RAY_QUERY_CANDIDATE_PROCEDURAL_PRIMITIVE);
        geometryMask |= 1u << enumerationQuery.CandidateGeometryIndex();
        CHECK(enumerationQuery.CandidatePrimitiveIndex() == 0);
        CHECK(enumerationQuery.CandidateInstanceIndex() == 0);
        CHECK(enumerationQuery.CandidateInstanceID() == 42);
        CHECK(enumerationQuery.CandidateInstanceContributionToHitGroupIndex() == 9);
        CHECK(
            enumerationQuery.CandidateProceduralPrimitiveNonOpaque() == (enumerationQuery.CandidateGeometryIndex() == 0)
        );
        ++candidateCount;
    }
    CHECK(candidateCount == 2);
    CHECK(geometryMask == 0x3);

    slang_prelude::RayQuery<0> commitQuery;
    commitQuery.TraceRayInline(shaderHandle, 0, 0x1, ray);
    REQUIRE(commitQuery.Proceed());
    CHECK(commitQuery.CandidateGeometryIndex() == 0);
    commitQuery.CommitProceduralPrimitiveHit(std::numeric_limits<float>::quiet_NaN());
    commitQuery.CommitProceduralPrimitiveHit(1.5f);
    commitQuery.CommitProceduralPrimitiveHit(1.25f);
    CHECK_FALSE(commitQuery.Proceed());
    CHECK(commitQuery.CommittedStatus() == slang_prelude::SLANG_RAY_QUERY_COMMITTED_PROCEDURAL_PRIMITIVE_HIT);
    CHECK(commitQuery.CommittedRayT() == doctest::Approx(1.25f));
    CHECK(commitQuery.CommittedGeometryIndex() == 0);
    CHECK(commitQuery.CommittedPrimitiveIndex() == 0);
    CHECK(commitQuery.CommittedObjectRayDirection().z == doctest::Approx(1.0f));

    // DXR gives procedural hits an inclusive interval, unlike the strict interval used for
    // triangles. A zero-length interval at the AABB entry point must therefore be committable.
    const TestRayDesc inclusiveBoundaryRay = {
        {0.0f, 0.0f, 0.0f},
        1.0f,
        {0.0f, 0.0f, 1.0f},
        1.0f,
    };
    slang_prelude::RayQuery<0> inclusiveBoundaryQuery;
    inclusiveBoundaryQuery.TraceRayInline(shaderHandle, 0, 0x1, inclusiveBoundaryRay);
    REQUIRE(inclusiveBoundaryQuery.Proceed());
    inclusiveBoundaryQuery.CommitProceduralPrimitiveHit(1.0f);
    CHECK_FALSE(inclusiveBoundaryQuery.Proceed());
    CHECK(
        inclusiveBoundaryQuery.CommittedStatus() == slang_prelude::SLANG_RAY_QUERY_COMMITTED_PROCEDURAL_PRIMITIVE_HIT
    );
    CHECK(inclusiveBoundaryQuery.CommittedRayT() == doctest::Approx(1.0f));

    slang_prelude::RayQuery<0> cullOpaqueQuery;
    cullOpaqueQuery.TraceRayInline(shaderHandle, slang_prelude::SLANG_RAY_QUERY_FLAG_CULL_OPAQUE, 0x1, ray);
    REQUIRE(cullOpaqueQuery.Proceed());
    CHECK(cullOpaqueQuery.CandidateGeometryIndex() == 0);
    CHECK_FALSE(cullOpaqueQuery.Proceed());

    slang_prelude::RayQuery<0> cullNonOpaqueQuery;
    cullNonOpaqueQuery.TraceRayInline(shaderHandle, slang_prelude::SLANG_RAY_QUERY_FLAG_CULL_NON_OPAQUE, 0x1, ray);
    REQUIRE(cullNonOpaqueQuery.Proceed());
    CHECK(cullNonOpaqueQuery.CandidateGeometryIndex() == 1);
    CHECK_FALSE(cullNonOpaqueQuery.CandidateProceduralPrimitiveNonOpaque());
    CHECK_FALSE(cullNonOpaqueQuery.Proceed());

    slang_prelude::RayQuery<0> skippedQuery;
    skippedQuery.TraceRayInline(shaderHandle, slang_prelude::SLANG_RAY_QUERY_FLAG_SKIP_PROCEDURAL_PRIMITIVES, 0x1, ray);
    CHECK_FALSE(skippedQuery.Proceed());
    CHECK(skippedQuery.CommittedStatus() == slang_prelude::SLANG_RAY_QUERY_COMMITTED_NOTHING);

    slang_prelude::RayQuery<0> acceptFirstQuery;
    acceptFirstQuery
        .TraceRayInline(shaderHandle, slang_prelude::SLANG_RAY_QUERY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH, 0x1, ray);
    REQUIRE(acceptFirstQuery.Proceed());
    acceptFirstQuery.CommitProceduralPrimitiveHit(1.25f);
    CHECK_FALSE(acceptFirstQuery.Proceed());
    CHECK(acceptFirstQuery.CommittedRayT() == doctest::Approx(1.25f));
}
