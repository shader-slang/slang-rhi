#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

// Minimal OptiX stand-ins to write device-args without depending on OptiX headers.
using CUdeviceptr = unsigned long long;
enum { OPTIX_CLUSTER_ACCEL_CLUSTER_FLAG_NONE = 0 };
enum { OPTIX_CLUSTER_ACCEL_INDICES_FORMAT_32BIT = 4 };
struct OptixClusterAccelPrimitiveInfo { unsigned int sbtIndex:24; unsigned int reserved:5; unsigned int primitiveFlags:3; };
struct OptixClusterAccelBuildInputTrianglesArgs
{
    unsigned int clusterId;
    unsigned int clusterFlags;
    unsigned int triangleCount              : 9;
    unsigned int vertexCount                : 9;
    unsigned int positionTruncateBitCount   : 6;
    unsigned int indexFormat                : 4;
    unsigned int opacityMicromapIndexFormat : 4;
    OptixClusterAccelPrimitiveInfo basePrimitiveInfo;
    unsigned short indexBufferStrideInBytes;
    unsigned short vertexBufferStrideInBytes;
    unsigned short primitiveInfoBufferStrideInBytes;
    unsigned short opacityMicromapIndexBufferStrideInBytes;
    CUdeviceptr indexBuffer;
    CUdeviceptr vertexBuffer;
    CUdeviceptr primitiveInfoBuffer;
    CUdeviceptr opacityMicromapArray;
    CUdeviceptr opacityMicromapIndexBuffer;
    CUdeviceptr instantiationBoundingBoxLimit;
};
struct OptixClusterAccelBuildInputClustersArgs
{
    unsigned int clusterHandlesCount;
    unsigned int clusterHandlesBufferStrideInBytes;
    CUdeviceptr  clusterHandlesBuffer;
};

static void requireClusterAccelOrSkip(IDevice* device)
{
    if (device->getDeviceType() != DeviceType::CUDA)
        SKIP("CUDA only test");
    if (device->getInfo().optixVersion < 90000)
        SKIP("requires OptiX 9+");
    if (!device->hasFeature(Feature::ClusterAccelerationStructure))
        SKIP("cluster acceleration structure not supported");
}

GPU_TEST_CASE("cluster-accel-sizes-optix", CUDA)
{
    requireClusterAccelOrSkip(device);

    ClusterAccelBuildDesc clasDesc = {};
    clasDesc.op = ClusterAccelBuildOp::CLASFromTriangles;
    clasDesc.trianglesLimits.maxArgCount = 1;
    clasDesc.trianglesLimits.maxTriangleCountPerArg = 1;
    clasDesc.trianglesLimits.maxVertexCountPerArg = 3;
    clasDesc.trianglesLimits.maxUniqueSbtIndexCountPerArg = 1;
    ClusterAccelSizes clasSizes = {};
    CHECK_CALL(device->getClusterAccelerationStructureSizes(clasDesc, &clasSizes));
    CHECK_GT(clasSizes.resultSize, 0);
    CHECK_GT(clasSizes.scratchSize, 0);

    ClusterAccelBuildDesc blasDesc = {};
    blasDesc.op = ClusterAccelBuildOp::BLASFromCLAS;
    blasDesc.clustersLimits.maxArgCount = 1;
    blasDesc.clustersLimits.maxTotalClusterCount = 1;
    blasDesc.clustersLimits.maxClusterCountPerArg = 1;
    ClusterAccelSizes blasSizes = {};
    CHECK_CALL(device->getClusterAccelerationStructureSizes(blasDesc, &blasSizes));
    CHECK_GT(blasSizes.resultSize, 0);
    CHECK_GT(blasSizes.scratchSize, 0);
}

GPU_TEST_CASE("cluster-accel-build-one-triangle", CUDA)
{
    requireClusterAccelOrSkip(device);

    struct Float3 { float x, y, z; };
    const Float3 vertices[3] = {{0.f,0.f,0.f},{1.f,0.f,0.f},{0.f,1.f,0.f}};
    const uint32_t indices[3] = {0,1,2};

    BufferDesc vDesc = {};
    vDesc.size = sizeof(vertices);
    vDesc.usage = BufferUsage::AccelerationStructureBuildInput;
    vDesc.defaultState = ResourceState::AccelerationStructureBuildInput;
    ComPtr<IBuffer> vbuf = device->createBuffer(vDesc, vertices);

    BufferDesc iDesc = {};
    iDesc.size = sizeof(indices);
    iDesc.usage = BufferUsage::AccelerationStructureBuildInput;
    iDesc.defaultState = ResourceState::AccelerationStructureBuildInput;
    ComPtr<IBuffer> ibuf = device->createBuffer(iDesc, indices);

    OptixClusterAccelBuildInputTrianglesArgs triArgs = {};
    triArgs.clusterId = 0;
    triArgs.clusterFlags = OPTIX_CLUSTER_ACCEL_CLUSTER_FLAG_NONE;
    triArgs.triangleCount = 1;
    triArgs.vertexCount = 3;
    triArgs.positionTruncateBitCount = 0;
    triArgs.indexFormat = OPTIX_CLUSTER_ACCEL_INDICES_FORMAT_32BIT;
    triArgs.opacityMicromapIndexFormat = 0;
    triArgs.basePrimitiveInfo = {};
    triArgs.indexBufferStrideInBytes = 0;
    triArgs.vertexBufferStrideInBytes = sizeof(Float3);
    triArgs.primitiveInfoBufferStrideInBytes = 0;
    triArgs.opacityMicromapIndexBufferStrideInBytes = 0;
    triArgs.indexBuffer = (CUdeviceptr)ibuf->getDeviceAddress();
    triArgs.vertexBuffer = (CUdeviceptr)vbuf->getDeviceAddress();
    triArgs.primitiveInfoBuffer = 0;
    triArgs.opacityMicromapArray = 0;
    triArgs.opacityMicromapIndexBuffer = 0;
    triArgs.instantiationBoundingBoxLimit = 0;

    BufferDesc argsDesc = {};
    argsDesc.size = sizeof(OptixClusterAccelBuildInputTrianglesArgs);
    argsDesc.usage = BufferUsage::AccelerationStructureBuildInput;
    argsDesc.defaultState = ResourceState::AccelerationStructureBuildInput;
    ComPtr<IBuffer> args = device->createBuffer(argsDesc, &triArgs);

    ClusterAccelBuildDesc clasDesc = {};
    clasDesc.op = ClusterAccelBuildOp::CLASFromTriangles;
    clasDesc.argsBuffer = BufferOffsetPair(args, 0);
    clasDesc.argsStride = sizeof(OptixClusterAccelBuildInputTrianglesArgs);
    clasDesc.argCount = 1;
    clasDesc.trianglesLimits.maxArgCount = 1;
    clasDesc.trianglesLimits.maxTriangleCountPerArg = 1;
    clasDesc.trianglesLimits.maxVertexCountPerArg = 3;
    clasDesc.trianglesLimits.maxUniqueSbtIndexCountPerArg = 1;

    ClusterAccelSizes clasSizes = {};
    CHECK_CALL(device->getClusterAccelerationStructureSizes(clasDesc, &clasSizes));

    BufferDesc resultDesc = {};
    resultDesc.size = clasSizes.resultSize;
    resultDesc.usage = BufferUsage::AccelerationStructure;
    resultDesc.defaultState = ResourceState::AccelerationStructure;
    ComPtr<IBuffer> clasResult = device->createBuffer(resultDesc);

    BufferDesc scratchDesc = {};
    scratchDesc.size = clasSizes.scratchSize;
    scratchDesc.usage = BufferUsage::UnorderedAccess;
    scratchDesc.defaultState = ResourceState::UnorderedAccess;
    ComPtr<IBuffer> clasScratch = device->createBuffer(scratchDesc);

    auto queue = device->getQueue(QueueType::Graphics);
    auto enc = queue->createCommandEncoder();
    enc->buildClusterAccelerationStructure(clasDesc, BufferOffsetPair(clasScratch, 0), BufferOffsetPair(clasResult, 0));
    queue->submit(enc->finish());
    queue->waitOnHost();

    uint64_t firstQword = 0;
    CHECK_CALL(device->readBuffer(clasResult, 0, sizeof(firstQword), &firstQword));
    CHECK_NE(firstQword, 0);

    OptixClusterAccelBuildInputClustersArgs clustersArgs = {};
    clustersArgs.clusterHandlesCount = 1;
    clustersArgs.clusterHandlesBufferStrideInBytes = 8;
    clustersArgs.clusterHandlesBuffer = (CUdeviceptr)clasResult->getDeviceAddress();

    BufferDesc blasArgsDesc = {};
    blasArgsDesc.size = sizeof(clustersArgs);
    blasArgsDesc.usage = BufferUsage::AccelerationStructureBuildInput;
    blasArgsDesc.defaultState = ResourceState::AccelerationStructureBuildInput;
    ComPtr<IBuffer> blasArgs = device->createBuffer(blasArgsDesc, &clustersArgs);

    ClusterAccelBuildDesc blasDesc = {};
    blasDesc.op = ClusterAccelBuildOp::BLASFromCLAS;
    blasDesc.argsBuffer = BufferOffsetPair(blasArgs, 0);
    blasDesc.argsStride = sizeof(OptixClusterAccelBuildInputClustersArgs);
    blasDesc.argCount = 1;
    blasDesc.clustersLimits.maxArgCount = 1;
    blasDesc.clustersLimits.maxTotalClusterCount = 1;
    blasDesc.clustersLimits.maxClusterCountPerArg = 1;

    ClusterAccelSizes blasSizes = {};
    CHECK_CALL(device->getClusterAccelerationStructureSizes(blasDesc, &blasSizes));

    BufferDesc blasResultDesc = {};
    blasResultDesc.size = blasSizes.resultSize;
    blasResultDesc.usage = BufferUsage::AccelerationStructure;
    blasResultDesc.defaultState = ResourceState::AccelerationStructure;
    ComPtr<IBuffer> blasResult = device->createBuffer(blasResultDesc);

    BufferDesc blasScratchDesc = {};
    blasScratchDesc.size = blasSizes.scratchSize;
    blasScratchDesc.usage = BufferUsage::UnorderedAccess;
    blasScratchDesc.defaultState = ResourceState::UnorderedAccess;
    ComPtr<IBuffer> blasScratch = device->createBuffer(blasScratchDesc);

    enc = queue->createCommandEncoder();
    enc->buildClusterAccelerationStructure(blasDesc, BufferOffsetPair(blasScratch, 0), BufferOffsetPair(blasResult, 0));
    queue->submit(enc->finish());
    queue->waitOnHost();

    uint64_t blasFirst = 0;
    CHECK_CALL(device->readBuffer(blasResult, 0, sizeof(blasFirst), &blasFirst));
    CHECK_NE(blasFirst, 0);
}

GPU_TEST_CASE("cluster-accel-batch-two-clusters", CUDA)
{
    requireClusterAccelOrSkip(device);

    struct Float3 { float x, y, z; };
    const Float3 vertices[6] = {{0,0,0},{1,0,0},{0,1,0}, {2,0,0},{3,0,0},{2,1,0}};
    const uint32_t indices[6] = {0,1,2, 0,1,2};

    BufferDesc vDesc = {};
    vDesc.size = sizeof(vertices);
    vDesc.usage = BufferUsage::AccelerationStructureBuildInput;
    vDesc.defaultState = ResourceState::AccelerationStructureBuildInput;
    ComPtr<IBuffer> vbuf = device->createBuffer(vDesc, vertices);

    BufferDesc iDesc = {};
    iDesc.size = sizeof(indices);
    iDesc.usage = BufferUsage::AccelerationStructureBuildInput;
    iDesc.defaultState = ResourceState::AccelerationStructureBuildInput;
    ComPtr<IBuffer> ibuf = device->createBuffer(iDesc, indices);

    OptixClusterAccelBuildInputTrianglesArgs triArgs[2] = {};
    for (int i = 0; i < 2; i++)
    {
        triArgs[i].clusterId = (unsigned)i;
        triArgs[i].clusterFlags = OPTIX_CLUSTER_ACCEL_CLUSTER_FLAG_NONE;
        triArgs[i].triangleCount = 1;
        triArgs[i].vertexCount = 3;
        triArgs[i].positionTruncateBitCount = 0;
        triArgs[i].indexFormat = OPTIX_CLUSTER_ACCEL_INDICES_FORMAT_32BIT;
        triArgs[i].opacityMicromapIndexFormat = 0;
        triArgs[i].basePrimitiveInfo = {};
        triArgs[i].indexBufferStrideInBytes = 0;
        triArgs[i].vertexBufferStrideInBytes = sizeof(Float3);
        triArgs[i].primitiveInfoBufferStrideInBytes = 0;
        triArgs[i].opacityMicromapIndexBufferStrideInBytes = 0;
        triArgs[i].indexBuffer = (CUdeviceptr)ibuf->getDeviceAddress() + (i * 3 * sizeof(uint32_t));
        triArgs[i].vertexBuffer = (CUdeviceptr)vbuf->getDeviceAddress() + (i * 3 * sizeof(Float3));
    }

    BufferDesc argsDesc = {};
    argsDesc.size = sizeof(triArgs);
    argsDesc.usage = BufferUsage::AccelerationStructureBuildInput;
    argsDesc.defaultState = ResourceState::AccelerationStructureBuildInput;
    ComPtr<IBuffer> args = device->createBuffer(argsDesc, &triArgs[0]);

    ClusterAccelBuildDesc clasDesc = {};
    clasDesc.op = ClusterAccelBuildOp::CLASFromTriangles;
    clasDesc.argsBuffer = BufferOffsetPair(args, 0);
    clasDesc.argsStride = sizeof(OptixClusterAccelBuildInputTrianglesArgs);
    clasDesc.argCount = 2;
    clasDesc.trianglesLimits.maxArgCount = 2;
    clasDesc.trianglesLimits.maxTriangleCountPerArg = 1;
    clasDesc.trianglesLimits.maxVertexCountPerArg = 3;
    clasDesc.trianglesLimits.maxUniqueSbtIndexCountPerArg = 1;

    ClusterAccelSizes clasSizes = {};
    CHECK_CALL(device->getClusterAccelerationStructureSizes(clasDesc, &clasSizes));

    BufferDesc resultDesc = {};
    resultDesc.size = clasSizes.resultSize;
    resultDesc.usage = BufferUsage::AccelerationStructure;
    resultDesc.defaultState = ResourceState::AccelerationStructure;
    ComPtr<IBuffer> clasResult = device->createBuffer(resultDesc);

    BufferDesc scratchDesc = {};
    scratchDesc.size = clasSizes.scratchSize;
    scratchDesc.usage = BufferUsage::UnorderedAccess;
    scratchDesc.defaultState = ResourceState::UnorderedAccess;
    ComPtr<IBuffer> clasScratch = device->createBuffer(scratchDesc);

    auto queue = device->getQueue(QueueType::Graphics);
    auto enc = queue->createCommandEncoder();
    enc->buildClusterAccelerationStructure(clasDesc, BufferOffsetPair(clasScratch, 0), BufferOffsetPair(clasResult, 0));
    queue->submit(enc->finish());
    queue->waitOnHost();

    uint64_t handles[2] = {};
    CHECK_CALL(device->readBuffer(clasResult, 0, sizeof(handles), &handles[0]));
    CHECK_NE(handles[0], 0);
    CHECK_NE(handles[1], 0);

    OptixClusterAccelBuildInputClustersArgs blasArgs = {};
    blasArgs.clusterHandlesCount = 2;
    blasArgs.clusterHandlesBufferStrideInBytes = 8;
    blasArgs.clusterHandlesBuffer = (CUdeviceptr)clasResult->getDeviceAddress();

    BufferDesc blasArgsDesc = {};
    blasArgsDesc.size = sizeof(blasArgs);
    blasArgsDesc.usage = BufferUsage::AccelerationStructureBuildInput;
    blasArgsDesc.defaultState = ResourceState::AccelerationStructureBuildInput;
    ComPtr<IBuffer> blasArgsBuf = device->createBuffer(blasArgsDesc, &blasArgs);

    ClusterAccelBuildDesc blasDesc = {};
    blasDesc.op = ClusterAccelBuildOp::BLASFromCLAS;
    blasDesc.argsBuffer = BufferOffsetPair(blasArgsBuf, 0);
    blasDesc.argsStride = sizeof(OptixClusterAccelBuildInputClustersArgs);
    blasDesc.argCount = 1;
    blasDesc.clustersLimits.maxArgCount = 1;
    blasDesc.clustersLimits.maxTotalClusterCount = 2;
    blasDesc.clustersLimits.maxClusterCountPerArg = 2;

    ClusterAccelSizes blasSizes = {};
    CHECK_CALL(device->getClusterAccelerationStructureSizes(blasDesc, &blasSizes));

    BufferDesc blasResultDesc = {};
    blasResultDesc.size = blasSizes.resultSize;
    blasResultDesc.usage = BufferUsage::AccelerationStructure;
    blasResultDesc.defaultState = ResourceState::AccelerationStructure;
    ComPtr<IBuffer> blasResult = device->createBuffer(blasResultDesc);

    BufferDesc blasScratchDesc = {};
    blasScratchDesc.size = blasSizes.scratchSize;
    blasScratchDesc.usage = BufferUsage::UnorderedAccess;
    blasScratchDesc.defaultState = ResourceState::UnorderedAccess;
    ComPtr<IBuffer> blasScratch = device->createBuffer(blasScratchDesc);

    enc = queue->createCommandEncoder();
    enc->buildClusterAccelerationStructure(blasDesc, BufferOffsetPair(blasScratch, 0), BufferOffsetPair(blasResult, 0));
    queue->submit(enc->finish());
    queue->waitOnHost();

    uint64_t firstQword = 0;
    CHECK_CALL(device->readBuffer(blasResult, 0, sizeof(firstQword), &firstQword));
    CHECK_NE(firstQword, 0);
}



