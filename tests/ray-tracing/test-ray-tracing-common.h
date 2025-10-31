#pragma once

#include "testing.h"
#include <slang-rhi/acceleration-structure-utils.h>
#include <vector>
#include <string>

// Mosts ray tracing tests only need:
// - a BLAS with simple geometry
// - a TLAS with a single instance
// - a pipeline
// - a straightforward shader table
// 
// This header provides classes and functions that provide these common building blocks.

namespace rhi::testing {
struct Vertex
{
    float position[3];
};

struct TriangleBlas
{
    ComPtr<IBuffer> vertexBuffer;
    ComPtr<IBuffer> indexBuffer;

    ComPtr<IBuffer> BLASBuffer;
    ComPtr<IAccelerationStructure> BLAS;

    TriangleBlas(
        IDevice* device,
        ICommandQueue* queue,
        int vertexCount,
        const Vertex* vertexData,
        int indexCount,
        const uint32_t* indexData
    )
    {
        BufferDesc vertexBufferDesc;
        vertexBufferDesc.size = vertexCount * sizeof(Vertex);
        vertexBufferDesc.usage = BufferUsage::AccelerationStructureBuildInput;
        vertexBufferDesc.defaultState = ResourceState::AccelerationStructureBuildInput;
        vertexBuffer = device->createBuffer(vertexBufferDesc, vertexData);
        REQUIRE(vertexBuffer != nullptr);

        BufferDesc indexBufferDesc;
        indexBufferDesc.size = indexCount * sizeof(int32_t);
        indexBufferDesc.usage = BufferUsage::AccelerationStructureBuildInput;
        indexBufferDesc.defaultState = ResourceState::AccelerationStructureBuildInput;
        indexBuffer = device->createBuffer(indexBufferDesc, indexData);
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
        buildDesc.flags = AccelerationStructureBuildFlags::AllowCompaction;

        // Query buffer size for acceleration structure build.
        AccelerationStructureSizes sizes;
        REQUIRE_CALL(device->getAccelerationStructureSizes(buildDesc, &sizes));

        // Allocate buffers for acceleration structure.
        BufferDesc scratchBufferDesc;
        scratchBufferDesc.usage = BufferUsage::UnorderedAccess;
        scratchBufferDesc.defaultState = ResourceState::UnorderedAccess;
        scratchBufferDesc.size = sizes.scratchSize;
        ComPtr<IBuffer> scratchBuffer = device->createBuffer(scratchBufferDesc);

        // Build acceleration structure.
        ComPtr<IQueryPool> compactedSizeQuery;
        QueryPoolDesc queryPoolDesc;
        queryPoolDesc.count = 1;
        queryPoolDesc.type = QueryType::AccelerationStructureCompactedSize;
        REQUIRE_CALL(device->createQueryPool(queryPoolDesc, compactedSizeQuery.writeRef()));

        ComPtr<IAccelerationStructure> draftAS;
        AccelerationStructureDesc draftCreateDesc;
        draftCreateDesc.size = sizes.accelerationStructureSize;
        REQUIRE_CALL(device->createAccelerationStructure(draftCreateDesc, draftAS.writeRef()));

        compactedSizeQuery->reset();

        auto commandEncoder = queue->createCommandEncoder();
        AccelerationStructureQueryDesc compactedSizeQueryDesc = {};
        compactedSizeQueryDesc.queryPool = compactedSizeQuery;
        compactedSizeQueryDesc.queryType = QueryType::AccelerationStructureCompactedSize;
        commandEncoder
            ->buildAccelerationStructure(buildDesc, draftAS, nullptr, scratchBuffer, 1, &compactedSizeQueryDesc);
        queue->submit(commandEncoder->finish());
        queue->waitOnHost();

        uint64_t compactedSize = 0;
        compactedSizeQuery->getResult(0, 1, &compactedSize);
        AccelerationStructureDesc createDesc;
        createDesc.size = compactedSize;
        device->createAccelerationStructure(createDesc, BLAS.writeRef());

        commandEncoder = queue->createCommandEncoder();
        commandEncoder->copyAccelerationStructure(BLAS, draftAS, AccelerationStructureCopyMode::Compact);
        queue->submit(commandEncoder->finish());
        queue->waitOnHost();
    }
};

struct SingleTriangleBlas : public TriangleBlas
{
    static const int kVertexCount = 3;
    inline static const Vertex kVertexData[kVertexCount] = {
        {0.f, 0.f, 1.f},
        {1.f, 0.f, 1.f},
        {0.f, 1.f, 1.f},
    };

    static const int kIndexCount = 3;
    inline static const uint32_t kIndexData[kIndexCount] = {0, 1, 2};

    SingleTriangleBlas(IDevice* device, ICommandQueue* queue)
        : TriangleBlas(device, queue, kVertexCount, &kVertexData[0], kIndexCount, &kIndexData[0])
    {
    }
};

struct ThreeTriangleBlas : public TriangleBlas
{
    static const int kVertexCount = 9;
    inline static const Vertex kVertexData[kVertexCount] = {
        // Triangle 1
        {0.f, 0.f, 1.f},
        {1.f, 0.f, 1.f},
        {0.f, 1.f, 1.f},

        // Triangle 2
        {0.f, 0.f, 1.f},
        {0.f, 1.f, 1.f},
        {-1.f, 0.f, 1.f},

        // Triangle 3
        {0.f, 0.f, 1.f},
        {1.f, 0.f, 1.f},
        {0.f, -1.f, 1.f},
    };

    static const int kIndexCount = 9;
    inline static const uint32_t kIndexData[kIndexCount] = {
        0,
        1,
        2,
        3,
        4,
        5,
        6,
        7,
        8,
    };

    ThreeTriangleBlas(IDevice* device, ICommandQueue* queue)
        : TriangleBlas(device, queue, kVertexCount, &kVertexData[0], kIndexCount, &kIndexData[0])
    {
    }
};

struct SphereBlas
{
    ComPtr<IBuffer> positionBuffer;
    ComPtr<IBuffer> radiusBuffer;

    ComPtr<IBuffer> BLASBuffer;
    ComPtr<IAccelerationStructure> BLAS;

    SphereBlas(
        IDevice* device,
        ICommandQueue* queue,
        int sphereCount,
        const Vertex* positionData,
        const float* radiusData
    )
    {
        BufferDesc positionBufferDesc;
        positionBufferDesc.size = sphereCount * sizeof(Vertex);
        positionBufferDesc.usage = BufferUsage::AccelerationStructureBuildInput;
        positionBufferDesc.defaultState = ResourceState::AccelerationStructureBuildInput;
        positionBuffer = device->createBuffer(positionBufferDesc, positionData);
        REQUIRE(positionBuffer != nullptr);

        BufferDesc radiusBufferDesc;
        radiusBufferDesc.size = sphereCount * sizeof(float);
        radiusBufferDesc.usage = BufferUsage::AccelerationStructureBuildInput;
        radiusBufferDesc.defaultState = ResourceState::AccelerationStructureBuildInput;
        radiusBuffer = device->createBuffer(radiusBufferDesc, radiusData);
        REQUIRE(radiusBuffer != nullptr);

        AccelerationStructureBuildInput buildInput = {};
        buildInput.type = AccelerationStructureBuildInputType::Spheres;
        buildInput.spheres.vertexBufferCount = 1;
        buildInput.spheres.vertexCount = sphereCount;
        buildInput.spheres.vertexPositionBuffers[0] = positionBuffer;
        buildInput.spheres.vertexPositionFormat = Format::RGB32Float;
        buildInput.spheres.vertexPositionStride = sizeof(Vertex);
        buildInput.spheres.vertexRadiusBuffers[0] = radiusBuffer;
        buildInput.spheres.vertexRadiusFormat = Format::R32Float;
        buildInput.spheres.vertexRadiusStride = sizeof(float);
        buildInput.spheres.flags = AccelerationStructureGeometryFlags::Opaque;

        AccelerationStructureBuildDesc buildDesc = {};
        buildDesc.inputs = &buildInput;
        buildDesc.inputCount = 1;
        buildDesc.flags = AccelerationStructureBuildFlags::AllowCompaction;

        // Query buffer size for acceleration structure build.
        AccelerationStructureSizes sizes;
        REQUIRE_CALL(device->getAccelerationStructureSizes(buildDesc, &sizes));

        // Allocate buffers for acceleration structure.
        BufferDesc scratchBufferDesc;
        scratchBufferDesc.usage = BufferUsage::UnorderedAccess;
        scratchBufferDesc.defaultState = ResourceState::UnorderedAccess;
        scratchBufferDesc.size = sizes.scratchSize;
        ComPtr<IBuffer> scratchBuffer = device->createBuffer(scratchBufferDesc);

        // Build acceleration structure.
        ComPtr<IQueryPool> compactedSizeQuery;
        QueryPoolDesc queryPoolDesc;
        queryPoolDesc.count = 1;
        queryPoolDesc.type = QueryType::AccelerationStructureCompactedSize;
        REQUIRE_CALL(device->createQueryPool(queryPoolDesc, compactedSizeQuery.writeRef()));

        ComPtr<IAccelerationStructure> draftAS;
        AccelerationStructureDesc draftCreateDesc;
        draftCreateDesc.size = sizes.accelerationStructureSize;
        REQUIRE_CALL(device->createAccelerationStructure(draftCreateDesc, draftAS.writeRef()));

        compactedSizeQuery->reset();

        auto commandEncoder = queue->createCommandEncoder();
        AccelerationStructureQueryDesc compactedSizeQueryDesc = {};
        compactedSizeQueryDesc.queryPool = compactedSizeQuery;
        compactedSizeQueryDesc.queryType = QueryType::AccelerationStructureCompactedSize;
        commandEncoder
            ->buildAccelerationStructure(buildDesc, draftAS, nullptr, scratchBuffer, 1, &compactedSizeQueryDesc);
        queue->submit(commandEncoder->finish());
        queue->waitOnHost();

        uint64_t compactedSize = 0;
        compactedSizeQuery->getResult(0, 1, &compactedSize);
        AccelerationStructureDesc createDesc;
        createDesc.size = compactedSize;
        device->createAccelerationStructure(createDesc, BLAS.writeRef());

        commandEncoder = queue->createCommandEncoder();
        commandEncoder->copyAccelerationStructure(BLAS, draftAS, AccelerationStructureCopyMode::Compact);
        queue->submit(commandEncoder->finish());
        queue->waitOnHost();
    }
};

struct SingleSphereBlas : public SphereBlas
{
    static const int kSphereCount = 1;
    inline static const Vertex kSphereData[kSphereCount] = {
        {0.f, 0.f, -3.f},
    };

    inline static const float kRadiusData[kSphereCount] = {2.f};

    SingleSphereBlas(IDevice* device, ICommandQueue* queue)
        : SphereBlas(device, queue, kSphereCount, kSphereData, kRadiusData)
    {
    }
};

struct ThreeSphereBlas : public SphereBlas
{
    static const int kSphereCount = 3;

    inline static const Vertex kSphereData[kSphereCount] = {
        {-0.5f, -0.5f, 3.0f},
        {0.5, -0.5f, 3.0f},
        {0.0f, 0.5f, 3.0f},
    };

    inline static const float kRadiusData[kSphereCount] = {0.4f, 0.2f, 0.6f};

    ThreeSphereBlas(IDevice* device, ICommandQueue* queue)
        : SphereBlas(device, queue, kSphereCount, kSphereData, kRadiusData)
    {
    }
};

struct SingleCustomGeometryBlas
{
    static const int kAabbCount = 1;
    inline static const AccelerationStructureAABB kAabbData[kAabbCount] = {
        {-0.5f, -0.5f, 0.5f, 0.5f, 0.5f, 1.0f},
    };

    ComPtr<IBuffer> aabbBuffer;

    ComPtr<IBuffer> BLASBuffer;
    ComPtr<IAccelerationStructure> BLAS;

    SingleCustomGeometryBlas(IDevice* device, ICommandQueue* queue)
    {
        BufferDesc aabbBufferDesc;
        aabbBufferDesc.size = sizeof(AccelerationStructureAABB);
        aabbBufferDesc.usage = BufferUsage::AccelerationStructureBuildInput;
        aabbBufferDesc.defaultState = ResourceState::AccelerationStructureBuildInput;
        aabbBuffer = device->createBuffer(aabbBufferDesc, &kAabbData[0]);
        REQUIRE(aabbBuffer != nullptr);

        AccelerationStructureBuildInput buildInput = {};
        buildInput.type = AccelerationStructureBuildInputType::ProceduralPrimitives;
        buildInput.proceduralPrimitives.aabbBuffers[0] = aabbBuffer;
        buildInput.proceduralPrimitives.aabbBufferCount = 1;
        buildInput.proceduralPrimitives.aabbStride = sizeof(AccelerationStructureAABB);
        buildInput.proceduralPrimitives.primitiveCount = kAabbCount;
        buildInput.proceduralPrimitives.flags = AccelerationStructureGeometryFlags::Opaque;

        AccelerationStructureBuildDesc buildDesc = {};
        buildDesc.inputs = &buildInput;
        buildDesc.inputCount = 1;
        buildDesc.flags = AccelerationStructureBuildFlags::AllowCompaction;

        // Query buffer size for acceleration structure build.
        AccelerationStructureSizes sizes;
        REQUIRE_CALL(device->getAccelerationStructureSizes(buildDesc, &sizes));

        // Allocate buffers for acceleration structure.
        BufferDesc scratchBufferDesc;
        scratchBufferDesc.usage = BufferUsage::UnorderedAccess;
        scratchBufferDesc.defaultState = ResourceState::UnorderedAccess;
        scratchBufferDesc.size = sizes.scratchSize;
        ComPtr<IBuffer> scratchBuffer = device->createBuffer(scratchBufferDesc);

        // Build acceleration structure.
        ComPtr<IQueryPool> compactedSizeQuery;
        QueryPoolDesc queryPoolDesc;
        queryPoolDesc.count = 1;
        queryPoolDesc.type = QueryType::AccelerationStructureCompactedSize;
        REQUIRE_CALL(device->createQueryPool(queryPoolDesc, compactedSizeQuery.writeRef()));

        ComPtr<IAccelerationStructure> draftAS;
        AccelerationStructureDesc draftCreateDesc;
        draftCreateDesc.size = sizes.accelerationStructureSize;
        REQUIRE_CALL(device->createAccelerationStructure(draftCreateDesc, draftAS.writeRef()));

        compactedSizeQuery->reset();

        auto commandEncoder = queue->createCommandEncoder();
        AccelerationStructureQueryDesc compactedSizeQueryDesc = {};
        compactedSizeQueryDesc.queryPool = compactedSizeQuery;
        compactedSizeQueryDesc.queryType = QueryType::AccelerationStructureCompactedSize;
        commandEncoder
            ->buildAccelerationStructure(buildDesc, draftAS, nullptr, scratchBuffer, 1, &compactedSizeQueryDesc);
        queue->submit(commandEncoder->finish());
        queue->waitOnHost();

        uint64_t compactedSize = 0;
        compactedSizeQuery->getResult(0, 1, &compactedSize);
        AccelerationStructureDesc createDesc;
        createDesc.size = compactedSize;
        device->createAccelerationStructure(createDesc, BLAS.writeRef());

        commandEncoder = queue->createCommandEncoder();
        commandEncoder->copyAccelerationStructure(BLAS, draftAS, AccelerationStructureCopyMode::Compact);
        queue->submit(commandEncoder->finish());
        queue->waitOnHost();
    }
};

struct LssBlas
{
    ComPtr<IBuffer> positionBuffer;
    ComPtr<IBuffer> radiusBuffer;
    ComPtr<IBuffer> indexBuffer;
 
    ComPtr<IBuffer> BLASBuffer;
    ComPtr<IAccelerationStructure> BLAS;

    LssBlas(IDevice* device, ICommandQueue* queue, int segmentCount, const Vertex* positionData, const float* radiusData, int primitiveCount, const unsigned* indexData)
    {
        BufferDesc positionBufferDesc;
        positionBufferDesc.size = segmentCount * sizeof(Vertex);
        positionBufferDesc.usage = BufferUsage::AccelerationStructureBuildInput;
        positionBufferDesc.defaultState = ResourceState::AccelerationStructureBuildInput;
        positionBuffer = device->createBuffer(positionBufferDesc, positionData);
        REQUIRE(positionBuffer != nullptr);

        BufferDesc radiusBufferDesc;
        radiusBufferDesc.size = segmentCount * sizeof(float);
        radiusBufferDesc.usage = BufferUsage::AccelerationStructureBuildInput;
        radiusBufferDesc.defaultState = ResourceState::AccelerationStructureBuildInput;
        radiusBuffer = device->createBuffer(radiusBufferDesc, radiusData);
        REQUIRE(radiusBuffer != nullptr);

        BufferDesc indexBufferDesc;
        indexBufferDesc.size = primitiveCount * sizeof(unsigned);
        indexBufferDesc.usage = BufferUsage::AccelerationStructureBuildInput;
        indexBufferDesc.defaultState = ResourceState::AccelerationStructureBuildInput;
        indexBuffer = device->createBuffer(indexBufferDesc, indexData);
        REQUIRE(indexBuffer != nullptr);

        AccelerationStructureBuildInput buildInput = {};
        buildInput.type = AccelerationStructureBuildInputType::LinearSweptSpheres;
        buildInput.linearSweptSpheres.primitiveCount = primitiveCount;
        buildInput.linearSweptSpheres.vertexBufferCount = 1;
        buildInput.linearSweptSpheres.vertexCount = segmentCount;
        buildInput.linearSweptSpheres.vertexPositionBuffers[0] = positionBuffer;
        buildInput.linearSweptSpheres.vertexPositionFormat = Format::RGB32Float;
        buildInput.linearSweptSpheres.vertexPositionStride = sizeof(Vertex);
        buildInput.linearSweptSpheres.vertexRadiusBuffers[0] = radiusBuffer;
        buildInput.linearSweptSpheres.vertexRadiusFormat = Format::R32Float;
        buildInput.linearSweptSpheres.vertexRadiusStride = sizeof(float);
        buildInput.linearSweptSpheres.indexingMode = LinearSweptSpheresIndexingMode::Successive;
        buildInput.linearSweptSpheres.indexBuffer = indexBuffer;
        buildInput.linearSweptSpheres.indexFormat = IndexFormat::Uint32;
        buildInput.linearSweptSpheres.indexCount = primitiveCount;
        buildInput.linearSweptSpheres.flags = AccelerationStructureGeometryFlags::Opaque;
        buildInput.linearSweptSpheres.endCapsMode = LinearSweptSpheresEndCapsMode::Chained;

        AccelerationStructureBuildDesc buildDesc = {};
        buildDesc.inputs = &buildInput;
        buildDesc.inputCount = 1;
        buildDesc.flags = AccelerationStructureBuildFlags::AllowCompaction;

        // Query buffer size for acceleration structure build.
        AccelerationStructureSizes sizes;
        REQUIRE_CALL(device->getAccelerationStructureSizes(buildDesc, &sizes));

        // Allocate buffers for acceleration structure.
        BufferDesc scratchBufferDesc;
        scratchBufferDesc.usage = BufferUsage::UnorderedAccess;
        scratchBufferDesc.defaultState = ResourceState::UnorderedAccess;
        scratchBufferDesc.size = sizes.scratchSize;
        ComPtr<IBuffer> scratchBuffer = device->createBuffer(scratchBufferDesc);

        // Build acceleration structure.
        ComPtr<IQueryPool> compactedSizeQuery;
        QueryPoolDesc queryPoolDesc;
        queryPoolDesc.count = 1;
        queryPoolDesc.type = QueryType::AccelerationStructureCompactedSize;
        REQUIRE_CALL(device->createQueryPool(queryPoolDesc, compactedSizeQuery.writeRef()));

        ComPtr<IAccelerationStructure> draftAS;
        AccelerationStructureDesc draftCreateDesc;
        draftCreateDesc.size = sizes.accelerationStructureSize;
        REQUIRE_CALL(device->createAccelerationStructure(draftCreateDesc, draftAS.writeRef()));

        compactedSizeQuery->reset();

        auto commandEncoder = queue->createCommandEncoder();
        AccelerationStructureQueryDesc compactedSizeQueryDesc = {};
        compactedSizeQueryDesc.queryPool = compactedSizeQuery;
        compactedSizeQueryDesc.queryType = QueryType::AccelerationStructureCompactedSize;
        commandEncoder
            ->buildAccelerationStructure(buildDesc, draftAS, nullptr, scratchBuffer, 1, &compactedSizeQueryDesc);
        queue->submit(commandEncoder->finish());
        queue->waitOnHost();

        uint64_t compactedSize = 0;
        compactedSizeQuery->getResult(0, 1, &compactedSize);
        AccelerationStructureDesc createDesc;
        createDesc.size = compactedSize;
        device->createAccelerationStructure(createDesc, BLAS.writeRef());

        commandEncoder = queue->createCommandEncoder();
        commandEncoder->copyAccelerationStructure(BLAS, draftAS, AccelerationStructureCopyMode::Compact);
        queue->submit(commandEncoder->finish());
        queue->waitOnHost();
    }
};

struct SingleSegmentLssBlas : public LssBlas
{
    static constexpr int kVertexCount = 2;
    static constexpr int kPrimitiveCount = 1;

    static inline constexpr Vertex kPositions[kVertexCount] = {
        {-0.5f, 0.0f, -3.0f},
        {0.5, 0.0f, -3.0f},
    };

    static inline constexpr float kRadii[kVertexCount] = {0.5f, 0.5f};

    static inline constexpr unsigned kIndices[kPrimitiveCount] = {0};


    SingleSegmentLssBlas(IDevice* device, ICommandQueue* queue)
        : LssBlas(device, queue, kVertexCount, kPositions, kRadii, kPrimitiveCount, kIndices)
    {
    }
};

struct TwoSegmentLssBlas : public LssBlas
{
    static constexpr int kVertexCount = 3;
    static constexpr int kPrimitiveCount = 2;

    static inline constexpr Vertex kPositions[kVertexCount] = {
        {-0.5f, -0.5f, 3.0f},
        {0.0, 0.5f, 3.0f},
        {0.5f, -0.5f, 3.0f},
    };

    static inline constexpr float kRadii[kVertexCount] = {0.2f, 0.2f, 0.2f};

    static inline constexpr unsigned kIndices[kPrimitiveCount] = {0, 1};


    TwoSegmentLssBlas(IDevice* device, ICommandQueue* queue)
        : LssBlas(device, queue, kVertexCount, kPositions, kRadii, kPrimitiveCount, kIndices)
    {
    }
};

struct Tlas
{
    ComPtr<IBuffer> instanceBuffer;
    ComPtr<IBuffer> TLASBuffer;
    ComPtr<IAccelerationStructure> TLAS;

    Tlas(IDevice* device, ICommandQueue* queue, IAccelerationStructure* BLAS)
    {
        AccelerationStructureInstanceDescType nativeInstanceDescType = getAccelerationStructureInstanceDescType(device);
        Size nativeInstanceDescSize = getAccelerationStructureInstanceDescSize(nativeInstanceDescType);

        std::vector<AccelerationStructureInstanceDescGeneric> genericInstanceDescs;
        genericInstanceDescs.resize(1);
        float transformMatrix[] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
        memcpy(&genericInstanceDescs[0].transform[0][0], transformMatrix, sizeof(float) * 12);
        genericInstanceDescs[0].instanceID = 0;
        genericInstanceDescs[0].instanceMask = 0xFF;
        genericInstanceDescs[0].instanceContributionToHitGroupIndex = 0;
        genericInstanceDescs[0].accelerationStructure = BLAS->getHandle();

        std::vector<uint8_t> nativeInstanceDescs(genericInstanceDescs.size() * nativeInstanceDescSize);
        convertAccelerationStructureInstanceDescs(
            genericInstanceDescs.size(),
            nativeInstanceDescType,
            nativeInstanceDescs.data(),
            nativeInstanceDescSize,
            genericInstanceDescs.data(),
            sizeof(AccelerationStructureInstanceDescGeneric)
        );

        BufferDesc instanceBufferDesc;
        instanceBufferDesc.size = nativeInstanceDescs.size();
        instanceBufferDesc.usage = BufferUsage::ShaderResource;
        instanceBufferDesc.defaultState = ResourceState::ShaderResource;
        instanceBuffer = device->createBuffer(instanceBufferDesc, nativeInstanceDescs.data());
        REQUIRE(instanceBuffer != nullptr);

        AccelerationStructureBuildInput buildInput = {};
        buildInput.type = AccelerationStructureBuildInputType::Instances;
        buildInput.instances.instanceBuffer = instanceBuffer;
        buildInput.instances.instanceCount = 1;
        buildInput.instances.instanceStride = nativeInstanceDescSize;
        AccelerationStructureBuildDesc buildDesc = {};
        buildDesc.inputs = &buildInput;
        buildDesc.inputCount = 1;

        // Query buffer size for acceleration structure build.
        AccelerationStructureSizes sizes;
        REQUIRE_CALL(device->getAccelerationStructureSizes(buildDesc, &sizes));

        BufferDesc scratchBufferDesc;
        scratchBufferDesc.usage = BufferUsage::UnorderedAccess;
        scratchBufferDesc.defaultState = ResourceState::UnorderedAccess;
        scratchBufferDesc.size = sizes.scratchSize;
        ComPtr<IBuffer> scratchBuffer = device->createBuffer(scratchBufferDesc);

        AccelerationStructureDesc createDesc;
        createDesc.size = sizes.accelerationStructureSize;
        REQUIRE_CALL(device->createAccelerationStructure(createDesc, TLAS.writeRef()));

        auto commandEncoder = queue->createCommandEncoder();
        commandEncoder->buildAccelerationStructure(buildDesc, TLAS, nullptr, scratchBuffer, 0, nullptr);
        queue->submit(commandEncoder->finish());
        queue->waitOnHost();
    }
};

struct ResultBuffer
{
    IDevice* device = nullptr;
    size_t bufferSize = 0;
    ComPtr<IBuffer> resultBuffer;

    ResultBuffer() = default;

    ResultBuffer(IDevice* device, size_t bufferSize)
        : device(device)
        , bufferSize(bufferSize)
    {
        BufferDesc resultBufferDesc = {};
        resultBufferDesc.size = bufferSize;
        resultBufferDesc.elementSize = bufferSize;
        resultBufferDesc.memoryType = MemoryType::DeviceLocal;
        resultBufferDesc.usage = BufferUsage::UnorderedAccess | BufferUsage::CopySource;
        resultBuffer = device->createBuffer(resultBufferDesc);
        REQUIRE(resultBuffer != nullptr);
    }

    void getFromDevice(ISlangBlob** resultBlob)
    {
        REQUIRE_CALL(device->readBuffer(resultBuffer, 0, bufferSize, resultBlob));
    }
};

Result loadShaderPrograms(
    IDevice* device,
    const char* moduleName,
    const std::vector<const char*>& programNames,
    IShaderProgram** outProgram
);

struct HitGroupProgramNames
{
    const char* closesthit = nullptr;
    const char* intersection = nullptr;
};

struct RayTracingTestPipeline
{
    ComPtr<IRayTracingPipeline> raytracingPipeline;
    ComPtr<IShaderTable> shaderTable;

    RayTracingTestPipeline(
        IDevice* device,
        const char* filepath,
        const std::vector<const char*>& raygenNames,
        const std::vector<HitGroupProgramNames>& programNames,
        const std::vector<const char*>& missNames,
        RayTracingPipelineFlags flags = RayTracingPipelineFlags::None
    )
    {
        ComPtr<IShaderProgram> rayTracingProgram;

        REQUIRE(raygenNames.size() > 0);
        REQUIRE(programNames.size() > 0);
        REQUIRE(missNames.size() > 0);

        std::vector<const char*> programsToLoad;
        for (const char* raygenName : raygenNames)
            programsToLoad.push_back(raygenName);

        for (const HitGroupProgramNames& programName : programNames)
        {
            programsToLoad.push_back(programName.closesthit);
            if (programName.intersection && strcmp(programName.intersection, "__builtin_intersection__sphere") != 0)
                programsToLoad.push_back(programName.intersection);
        }

        for (const char* missName : missNames)
            programsToLoad.push_back(missName);

        REQUIRE_CALL(loadShaderPrograms(device, filepath, programsToLoad, rayTracingProgram.writeRef()));

        std::vector<std::string> hitgroupNames;
        for (unsigned int i = 0; i < programNames.size(); i++)
            hitgroupNames.push_back("hitgroup" + std::to_string(i + 1));

        std::vector<const char*> hitgroupNamesCstr;
        for (const std::string& hitgroupName : hitgroupNames)
            hitgroupNamesCstr.push_back(hitgroupName.c_str());

        std::vector<HitGroupDesc> hitGroups;

        for (unsigned int i = 0; i < programNames.size(); i++)
        {
            HitGroupDesc hitGroup{};
            hitGroup.hitGroupName = hitgroupNamesCstr[i];
            hitGroup.closestHitEntryPoint = programNames[i].closesthit;
            hitGroup.intersectionEntryPoint = programNames[i].intersection;

            hitGroups.push_back(hitGroup);
        }

        RayTracingPipelineDesc rtpDesc = {};
        rtpDesc.program = rayTracingProgram;
        rtpDesc.hitGroupCount = hitGroups.size();
        rtpDesc.hitGroups = hitGroups.data();
        rtpDesc.maxRayPayloadSize = 64;
        rtpDesc.maxAttributeSizeInBytes = 8;
        rtpDesc.maxRecursion = 2;
        rtpDesc.flags = flags;
        REQUIRE_CALL(device->createRayTracingPipeline(rtpDesc, raytracingPipeline.writeRef()));
        REQUIRE(raytracingPipeline != nullptr);


        ShaderTableDesc shaderTableDesc = {};
        shaderTableDesc.program = rayTracingProgram;
        shaderTableDesc.hitGroupCount = hitgroupNames.size();
        shaderTableDesc.hitGroupNames = hitgroupNamesCstr.data();
        shaderTableDesc.rayGenShaderCount = raygenNames.size();
        shaderTableDesc.rayGenShaderEntryPointNames = const_cast<const char**>(raygenNames.data());
        shaderTableDesc.missShaderCount = missNames.size();
        shaderTableDesc.missShaderEntryPointNames = const_cast<const char**>(missNames.data());
        REQUIRE_CALL(device->createShaderTable(shaderTableDesc, shaderTable.writeRef()));
    }
};

void launchPipeline(
    ICommandQueue* queue,
    IRayTracingPipeline* pipeline,
    IShaderTable* shaderTable,
    IBuffer* resultBuffer,
    IAccelerationStructure* TLAS
);

} // namespace rhi::testing