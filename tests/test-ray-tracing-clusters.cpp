#include "testing.h"
#include <slang-rhi/acceleration-structure-utils.h>
#include <slang-rhi/cluster_accel_abi_host.h>

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
    // Implicit: set required buffers in desc
    auto alignUp = [](uint64_t v, uint64_t a) { return (v + (a - 1)) & ~(a - 1); };
    uint64_t handlesBytes = alignUp((uint64_t)cluster_abi::CLUSTER_HANDLE_BYTE_STRIDE, (uint64_t)cluster_abi::CLUSTER_OUTPUT_ALIGNMENT);
    clasDesc.mode = ClusterAccelBuildDesc::BuildMode::Implicit;
    clasDesc.modeDesc.implicit.outputHandlesBuffer = (DeviceAddress)clasResult->getDeviceAddress();
    clasDesc.modeDesc.implicit.outputHandlesStrideInBytes = 0;
    clasDesc.modeDesc.implicit.outputBuffer = (DeviceAddress)clasResult->getDeviceAddress() + handlesBytes;
    clasDesc.modeDesc.implicit.outputBufferSizeInBytes = resultDesc.size;
    clasDesc.modeDesc.implicit.tempBuffer = (DeviceAddress)clasScratch->getDeviceAddress();
    clasDesc.modeDesc.implicit.tempBufferSizeInBytes = scratchDesc.size;
    enc->buildClusterAccelerationStructure(clasDesc);
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
    // Implicit: set required buffers in desc for BLAS
    uint64_t blasHandlesBytes = cluster_abi::CLUSTER_HANDLE_BYTE_STRIDE;
    uint64_t blasHandlesPad128 = alignUp(blasHandlesBytes, (uint64_t)cluster_abi::CLUSTER_OUTPUT_ALIGNMENT);
    blasDesc.mode = ClusterAccelBuildDesc::BuildMode::Implicit;
    blasDesc.modeDesc.implicit.outputHandlesBuffer = (DeviceAddress)blasResult->getDeviceAddress();
    blasDesc.modeDesc.implicit.outputHandlesStrideInBytes = 0;
    blasDesc.modeDesc.implicit.outputBuffer = (DeviceAddress)blasResult->getDeviceAddress() + blasHandlesPad128;
    blasDesc.modeDesc.implicit.outputBufferSizeInBytes = blasResultDesc.size;
    blasDesc.modeDesc.implicit.tempBuffer = (DeviceAddress)blasScratch->getDeviceAddress();
    blasDesc.modeDesc.implicit.tempBufferSizeInBytes = blasScratchDesc.size;
    enc->buildClusterAccelerationStructure(blasDesc);
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
    // Implicit: set required buffers in desc
    auto alignUp = [](uint64_t v, uint64_t a) { return (v + (a - 1)) & ~(a - 1); };
    uint64_t handlesBytes = alignUp((uint64_t)clasDesc.argCount * (uint64_t)cluster_abi::CLUSTER_HANDLE_BYTE_STRIDE,
                                    (uint64_t)cluster_abi::CLUSTER_OUTPUT_ALIGNMENT);
    clasDesc.mode = ClusterAccelBuildDesc::BuildMode::Implicit;
    clasDesc.modeDesc.implicit.outputHandlesBuffer = (DeviceAddress)clasResult->getDeviceAddress();
    clasDesc.modeDesc.implicit.outputHandlesStrideInBytes = 0;
    clasDesc.modeDesc.implicit.outputBuffer = (DeviceAddress)clasResult->getDeviceAddress() + handlesBytes;
    clasDesc.modeDesc.implicit.outputBufferSizeInBytes = resultDesc.size;
    clasDesc.modeDesc.implicit.tempBuffer = (DeviceAddress)clasScratch->getDeviceAddress();
    clasDesc.modeDesc.implicit.tempBufferSizeInBytes = scratchDesc.size;
    enc->buildClusterAccelerationStructure(clasDesc);
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
    // Implicit: set required buffers in desc for BLAS
    uint64_t blasHandlesBytes = cluster_abi::CLUSTER_HANDLE_BYTE_STRIDE;
    uint64_t blasHandlesPad128 = alignUp(blasHandlesBytes, (uint64_t)cluster_abi::CLUSTER_OUTPUT_ALIGNMENT);
    blasDesc.mode = ClusterAccelBuildDesc::BuildMode::Implicit;
    blasDesc.modeDesc.implicit.outputHandlesBuffer = (DeviceAddress)blasResult->getDeviceAddress();
    blasDesc.modeDesc.implicit.outputHandlesStrideInBytes = 0;
    blasDesc.modeDesc.implicit.outputBuffer = (DeviceAddress)blasResult->getDeviceAddress() + blasHandlesPad128;
    blasDesc.modeDesc.implicit.outputBufferSizeInBytes = blasResultDesc.size;
    blasDesc.modeDesc.implicit.tempBuffer = (DeviceAddress)blasScratch->getDeviceAddress();
    blasDesc.modeDesc.implicit.tempBufferSizeInBytes = blasScratchDesc.size;
    enc->buildClusterAccelerationStructure(blasDesc);
    queue->submit(enc->finish());
    queue->waitOnHost();

    uint64_t firstQword = 0;
    CHECK_CALL(device->readBuffer(blasResult, 0, sizeof(firstQword), &firstQword));
    CHECK_NE(firstQword, 0);
}


GPU_TEST_CASE("cluster-accel-explicit-two-clusters", CUDA)
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

    // Scratch sizing (used for both get-sizes and explicit build)
    ClusterAccelSizes clasSizes = {};
    CHECK_CALL(device->getClusterAccelerationStructureSizes(clasDesc, &clasSizes));

    BufferDesc scratchDesc = {};
    scratchDesc.size = clasSizes.scratchSize;
    scratchDesc.usage = BufferUsage::UnorderedAccess;
    scratchDesc.defaultState = ResourceState::UnorderedAccess;
    ComPtr<IBuffer> clasScratch = device->createBuffer(scratchDesc);

    // Step 1: GET_SIZES to produce per-CLAS sizes
    uint32_t zeroSizes[2] = {0,0};
    BufferDesc sizesDesc = {};
    sizesDesc.size = sizeof(uint32_t) * 2;
    sizesDesc.usage = BufferUsage::UnorderedAccess;
    sizesDesc.defaultState = ResourceState::UnorderedAccess;
    ComPtr<IBuffer> sizesBuf = device->createBuffer(sizesDesc, zeroSizes);

    clasDesc.mode = ClusterAccelBuildDesc::BuildMode::GetSizes;
    clasDesc.modeDesc.getSizes.outputSizesBuffer = sizesBuf->getDeviceAddress();
    clasDesc.modeDesc.getSizes.outputSizesStrideInBytes = 0; // 0 -> 4
    // Required temp for GetSizes
    clasDesc.modeDesc.getSizes.tempBuffer = clasScratch->getDeviceAddress();
    clasDesc.modeDesc.getSizes.tempBufferSizeInBytes = scratchDesc.size;

    // Build with GET_SIZES; result buffer unused, provide a small dummy
    BufferDesc dummyOutDesc = {};
    dummyOutDesc.size = 256;
    dummyOutDesc.usage = BufferUsage::AccelerationStructure;
    dummyOutDesc.defaultState = ResourceState::AccelerationStructure;
    ComPtr<IBuffer> dummyOut = device->createBuffer(dummyOutDesc);

    auto queue = device->getQueue(QueueType::Graphics);
    auto enc = queue->createCommandEncoder();
    enc->buildClusterAccelerationStructure(clasDesc);
    queue->submit(enc->finish());
    queue->waitOnHost();

    uint32_t sizesHost[2] = {};
    CHECK_CALL(device->readBuffer(sizesBuf, 0, sizeof(sizesHost), &sizesHost[0]));
    CHECK_GT(sizesHost[0], 0);
    CHECK_GT(sizesHost[1], 0);

    // Step 2: allocate per-CLAS destinations in a single arena and create destAddresses array
    auto alignUp = [](uint64_t v, uint64_t a) { return (v + (a - 1)) & ~(a - 1); };
    uint64_t off0 = 0;
    uint64_t off1 = alignUp(uint64_t(sizesHost[0]), (uint64_t)cluster_abi::CLUSTER_OUTPUT_ALIGNMENT);
    uint64_t totalArena = off1 + alignUp(uint64_t(sizesHost[1]), (uint64_t)cluster_abi::CLUSTER_OUTPUT_ALIGNMENT);

    BufferDesc arenaDesc = {};
    arenaDesc.size = totalArena;
    arenaDesc.usage = BufferUsage::AccelerationStructure;
    arenaDesc.defaultState = ResourceState::AccelerationStructure;
    ComPtr<IBuffer> arena = device->createBuffer(arenaDesc);

    uint64_t destAddrsHost[2] = {
        arena->getDeviceAddress() + off0,
        arena->getDeviceAddress() + off1,
    };
    BufferDesc destAddrDesc = {};
    destAddrDesc.size = sizeof(destAddrsHost);
    destAddrDesc.usage = BufferUsage::UnorderedAccess;
    destAddrDesc.defaultState = ResourceState::UnorderedAccess;
    ComPtr<IBuffer> destAddrBuf = device->createBuffer(destAddrDesc, destAddrsHost);

    // Step 3: Explicit build, alias handles to destAddresses array
    clasDesc.mode = ClusterAccelBuildDesc::BuildMode::Explicit;
    clasDesc.modeDesc.explicitDest.destAddressesBuffer = destAddrBuf->getDeviceAddress();
    clasDesc.modeDesc.explicitDest.destAddressesStrideInBytes = 0; // 0 -> 8
    clasDesc.modeDesc.explicitDest.outputHandlesBuffer = 0;        // alias to destAddresses per spec
    clasDesc.modeDesc.explicitDest.outputHandlesStrideInBytes = 0; // 0->8
    clasDesc.modeDesc.explicitDest.outputSizesBuffer = 0;
    clasDesc.modeDesc.explicitDest.outputSizesStrideInBytes = 0;

    enc = queue->createCommandEncoder();
    enc->buildClusterAccelerationStructure(clasDesc);
    queue->submit(enc->finish());
    queue->waitOnHost();

    uint64_t handles[2] = {};
    CHECK_CALL(device->readBuffer(destAddrBuf, 0, sizeof(handles), &handles[0]));
    CHECK_NE(handles[0], 0);
    CHECK_NE(handles[1], 0);
    // Aliasing semantics: handles should match the destination addresses we provided
    CHECK_EQ(handles[0], destAddrsHost[0]);
    CHECK_EQ(handles[1], destAddrsHost[1]);
}




// Build two clusters with device-written TrianglesArgs and render two horizontal bands.
GPU_TEST_CASE("cluster-accel-build-and-shoot-device-args", CUDA)
{
    requireClusterAccelOrSkip(device);

    // Geometry: two horizontal strips (shared indices; per-cluster vertex base)
    struct Float3 { float x, y, z; };
    constexpr uint32_t kGridW = 4;
    constexpr uint32_t kGridH = 1;
    constexpr uint32_t triCount = kGridW * kGridH * 2; // 8
    constexpr uint32_t vertW = kGridW + 1;             // 5
    constexpr uint32_t vertH = kGridH + 1;             // 2
    constexpr uint32_t vertCount = vertW * vertH;      // 10
    const uint32_t triPerCluster = triCount;
    const uint32_t vtxPerCluster = vertCount;

    // Strip geometry parameters
    constexpr float kStripXMin = -0.8f;
    constexpr float kStripXSpan = 1.6f;
    constexpr float kStripHeight = 0.4f;

    std::vector<Float3> vertices;
    vertices.reserve(vertCount * 2);
    for (uint32_t j = 0; j < vertH; ++j)
    {
        for (uint32_t i = 0; i < vertW; ++i)
        {
            float u = float(i) / float(kGridW);
            float v = float(j) / float(kGridH);
            float x = kStripXMin + u * kStripXSpan;
            float y = -0.2f + v * kStripHeight;
            vertices.push_back({x, y, 0.0f});
        }
    }
    // Second strip above the first
    constexpr float kStripGap = 0.25f;
    for (uint32_t i = 0; i < vertCount; ++i)
    {
        Float3 vtx = vertices[i];
        vtx.y += kStripHeight + kStripGap;
        vertices.push_back(vtx);
    }

    std::vector<uint32_t> indices;
    indices.reserve(triCount * 3);
    for (uint32_t j = 0; j < kGridH; ++j)
    {
        for (uint32_t i = 0; i < kGridW; ++i)
        {
            uint32_t i0 = j * vertW + i;
            uint32_t i1 = j * vertW + (i + 1);
            uint32_t i2 = (j + 1) * vertW + i;
            uint32_t i3 = (j + 1) * vertW + (i + 1);
            // Two triangles per cell
            indices.push_back(i0); indices.push_back(i1); indices.push_back(i3);
            indices.push_back(i3); indices.push_back(i2); indices.push_back(i0);
        }
    }

    BufferDesc vDesc = {};
    vDesc.size = vertices.size() * sizeof(Float3);
    vDesc.usage = BufferUsage::AccelerationStructureBuildInput;
    vDesc.defaultState = ResourceState::AccelerationStructureBuildInput;
    ComPtr<IBuffer> vbuf = device->createBuffer(vDesc, vertices.data());

    BufferDesc iDesc = {};
    iDesc.size = indices.size() * sizeof(uint32_t);
    iDesc.usage = BufferUsage::AccelerationStructureBuildInput;
    iDesc.defaultState = ResourceState::AccelerationStructureBuildInput;
    ComPtr<IBuffer> ibuf = device->createBuffer(iDesc, indices.data());

    // Device-written args buffer for two clusters
    const uint32_t clusterCount = 2;
    BufferDesc argsDesc = {};
    argsDesc.size = uint64_t(clusterCount) * sizeof(cluster_abi::TrianglesArgs);
    argsDesc.usage = BufferUsage::AccelerationStructureBuildInput | BufferUsage::UnorderedAccess;
    argsDesc.defaultState = ResourceState::UnorderedAccess;
    ComPtr<IBuffer> triArgsBuf = device->createBuffer(argsDesc);

    // Compute: write TrianglesArgs on device
    ComPtr<IShaderProgram> computeProgram;
    slang::ProgramLayout* slangReflection = nullptr;
    REQUIRE_CALL(loadComputeProgram(device, computeProgram, "test-ray-tracing-clusters", "write_tri_args", slangReflection));
    ComputePipelineDesc cdesc = {};
    cdesc.program = computeProgram;
    ComPtr<IComputePipeline> cpipeline;
    REQUIRE_CALL(device->createComputePipeline(cdesc, cpipeline.writeRef()));

    auto queue = device->getQueue(QueueType::Graphics);
    auto enc = queue->createCommandEncoder();
    {
        auto cpass = enc->beginComputePass();
        auto root = cpass->bindPipeline(cpipeline);
        ShaderCursor cur(root);
        cur["g_tri_args"].setBinding(triArgsBuf);
        uint64_t idxAddr = ibuf->getDeviceAddress();
        uint64_t vtxAddr = vbuf->getDeviceAddress();
        uint32_t vtxStride = (uint32_t)sizeof(Float3);
        uint32_t vtxOffsetElemsPerCluster = vertCount;
        cur["g_index_buffer"].setData(&idxAddr, sizeof(idxAddr));
        cur["g_vertex_buffer"].setData(&vtxAddr, sizeof(vtxAddr));
        cur["g_vertex_stride_bytes"].setData(&vtxStride, sizeof(vtxStride));
        cur["g_triangle_count"].setData(&triPerCluster, sizeof(triPerCluster));
        cur["g_vertex_count"].setData(&vtxPerCluster, sizeof(vtxPerCluster));
        cur["g_vertex_offset_elems_per_cluster"].setData(&vtxOffsetElemsPerCluster, sizeof(vtxOffsetElemsPerCluster));
        cur["g_cluster_count"].setData(&clusterCount, sizeof(clusterCount));
        cpass->dispatchCompute(2, 1, 1);
        cpass->end();
    }
    enc->globalBarrier();

    // Build CLAS (implicit mode) using device-written args
    ClusterAccelBuildDesc clasDesc = {};
    clasDesc.op = ClusterAccelBuildOp::CLASFromTriangles;
    clasDesc.argsBuffer = BufferOffsetPair(triArgsBuf, 0);
    clasDesc.argsStride = (uint32_t)sizeof(cluster_abi::TrianglesArgs);
    clasDesc.argCount = clusterCount;
    clasDesc.trianglesLimits.maxArgCount = clusterCount;
    clasDesc.trianglesLimits.maxTriangleCountPerArg = triPerCluster;
    clasDesc.trianglesLimits.maxVertexCountPerArg = vtxPerCluster;
    clasDesc.trianglesLimits.maxUniqueSbtIndexCountPerArg = 1;

    ClusterAccelSizes clasSizes = {};
    REQUIRE_CALL(device->getClusterAccelerationStructureSizes(clasDesc, &clasSizes));

    auto alignUp = [](uint64_t v, uint64_t a) { return (v + (a - 1)) & ~(a - 1); };
    BufferDesc clasOutDesc = {};
    uint64_t handlesBytes = uint64_t(clusterCount) * 8u;
    uint64_t handlesPad128 = alignUp(handlesBytes, (uint64_t)cluster_abi::CLUSTER_OUTPUT_ALIGNMENT);
    clasOutDesc.size = handlesPad128 + clasSizes.resultSize;
    clasOutDesc.usage = BufferUsage::AccelerationStructure;
    clasOutDesc.defaultState = ResourceState::AccelerationStructure;
    ComPtr<IBuffer> clasOut = device->createBuffer(clasOutDesc);

    BufferDesc clasScratchDesc = {};
    clasScratchDesc.size = clasSizes.scratchSize;
    clasScratchDesc.usage = BufferUsage::UnorderedAccess;
    clasScratchDesc.defaultState = ResourceState::UnorderedAccess;
    ComPtr<IBuffer> clasScratch = device->createBuffer(clasScratchDesc);

    clasDesc.mode = ClusterAccelBuildDesc::BuildMode::Implicit;
    clasDesc.modeDesc.implicit.outputHandlesBuffer = (DeviceAddress)clasOut->getDeviceAddress();
    clasDesc.modeDesc.implicit.outputHandlesStrideInBytes = 0;
    clasDesc.modeDesc.implicit.outputBuffer = (DeviceAddress)clasOut->getDeviceAddress() + handlesPad128;
    clasDesc.modeDesc.implicit.outputBufferSizeInBytes = clasOutDesc.size;
    clasDesc.modeDesc.implicit.tempBuffer = (DeviceAddress)clasScratch->getDeviceAddress();
    clasDesc.modeDesc.implicit.tempBufferSizeInBytes = clasScratchDesc.size;

    enc->buildClusterAccelerationStructure(clasDesc);
    queue->submit(enc->finish());
    queue->waitOnHost();

    uint64_t clasHandles[2] = {};
    REQUIRE_CALL(device->readBuffer(clasOut, 0, sizeof(clasHandles), &clasHandles[0]));
    CHECK_NE(clasHandles[0], 0);
    CHECK_NE(clasHandles[1], 0);

    // Build BLAS from CLAS handles
    OptixClusterAccelBuildInputClustersArgs clustersArgs = {};
    clustersArgs.clusterHandlesCount = clusterCount;
    clustersArgs.clusterHandlesBufferStrideInBytes = cluster_abi::CLUSTER_HANDLE_BYTE_STRIDE;
    clustersArgs.clusterHandlesBuffer = (CUdeviceptr)clasOut->getDeviceAddress();

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
    blasDesc.clustersLimits.maxTotalClusterCount = clusterCount;
    blasDesc.clustersLimits.maxClusterCountPerArg = clusterCount;

    ClusterAccelSizes blasSizes = {};
    REQUIRE_CALL(device->getClusterAccelerationStructureSizes(blasDesc, &blasSizes));

    BufferDesc blasOutDesc = {};
    uint64_t blasHandlesBytes = cluster_abi::CLUSTER_HANDLE_BYTE_STRIDE;
    uint64_t blasHandlesPad128 = alignUp(blasHandlesBytes, (uint64_t)cluster_abi::CLUSTER_OUTPUT_ALIGNMENT);
    blasOutDesc.size = blasHandlesPad128 + blasSizes.resultSize;
    blasOutDesc.usage = BufferUsage::AccelerationStructure;
    blasOutDesc.defaultState = ResourceState::AccelerationStructure;
    ComPtr<IBuffer> blasOut = device->createBuffer(blasOutDesc);

    BufferDesc blasScratchDesc = {};
    blasScratchDesc.size = blasSizes.scratchSize;
    blasScratchDesc.usage = BufferUsage::UnorderedAccess;
    blasScratchDesc.defaultState = ResourceState::UnorderedAccess;
    ComPtr<IBuffer> blasScratch = device->createBuffer(blasScratchDesc);

    enc = queue->createCommandEncoder();
    blasDesc.mode = ClusterAccelBuildDesc::BuildMode::Implicit;
    blasDesc.modeDesc.implicit.outputHandlesBuffer = (DeviceAddress)blasOut->getDeviceAddress();
    blasDesc.modeDesc.implicit.outputHandlesStrideInBytes = 0;
    blasDesc.modeDesc.implicit.outputBuffer = (DeviceAddress)blasOut->getDeviceAddress() + blasHandlesPad128;
    blasDesc.modeDesc.implicit.outputBufferSizeInBytes = blasOutDesc.size;
    blasDesc.modeDesc.implicit.tempBuffer = (DeviceAddress)blasScratch->getDeviceAddress();
    blasDesc.modeDesc.implicit.tempBufferSizeInBytes = blasSizes.scratchSize;
    enc->buildClusterAccelerationStructure(blasDesc);
    queue->submit(enc->finish());
    queue->waitOnHost();

    uint64_t blasHandle = 0;
    REQUIRE_CALL(device->readBuffer(blasOut, 0, sizeof(blasHandle), &blasHandle));
    CHECK_NE(blasHandle, 0);

    // Build TLAS from BLAS handle
    AccelerationStructureInstanceDescType nativeInstanceDescType = getAccelerationStructureInstanceDescType(device);
    Size nativeInstanceDescSize = getAccelerationStructureInstanceDescSize(nativeInstanceDescType);
    std::vector<AccelerationStructureInstanceDescGeneric> genericInstanceDescs(1);
    float transformMatrix[] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    memcpy(&genericInstanceDescs[0].transform[0][0], transformMatrix, sizeof(float) * 12);
    genericInstanceDescs[0].instanceID = 0;
    genericInstanceDescs[0].instanceMask = 0xff;
    genericInstanceDescs[0].instanceContributionToHitGroupIndex = 0;
    genericInstanceDescs[0].accelerationStructure = AccelerationStructureHandle{blasHandle};

    std::vector<uint8_t> nativeInstanceDescs(genericInstanceDescs.size() * nativeInstanceDescSize);
    convertAccelerationStructureInstanceDescs(
        genericInstanceDescs.size(),
        nativeInstanceDescType,
        nativeInstanceDescs.data(),
        nativeInstanceDescSize,
        genericInstanceDescs.data(),
        sizeof(AccelerationStructureInstanceDescGeneric));

    BufferDesc instanceBufferDesc = {};
    instanceBufferDesc.size = nativeInstanceDescs.size();
    instanceBufferDesc.usage = BufferUsage::ShaderResource;
    instanceBufferDesc.defaultState = ResourceState::ShaderResource;
    ComPtr<IBuffer> instanceBuffer = device->createBuffer(instanceBufferDesc, nativeInstanceDescs.data());

    AccelerationStructureBuildInput buildInput = {};
    buildInput.type = AccelerationStructureBuildInputType::Instances;
    buildInput.instances.instanceBuffer = instanceBuffer;
    buildInput.instances.instanceCount = 1;
    buildInput.instances.instanceStride = nativeInstanceDescSize;
    AccelerationStructureBuildDesc tlasBuildDesc = {};
    tlasBuildDesc.inputs = &buildInput;
    tlasBuildDesc.inputCount = 1;

    AccelerationStructureSizes tlasSizes = {};
    REQUIRE_CALL(device->getAccelerationStructureSizes(tlasBuildDesc, &tlasSizes));
    BufferDesc tlasScratchDesc = {};
    tlasScratchDesc.usage = BufferUsage::UnorderedAccess;
    tlasScratchDesc.defaultState = ResourceState::UnorderedAccess;
    tlasScratchDesc.size = tlasSizes.scratchSize;
    ComPtr<IBuffer> tlasScratch = device->createBuffer(tlasScratchDesc);

    AccelerationStructureDesc createDesc = {};
    createDesc.size = tlasSizes.accelerationStructureSize;
    ComPtr<IAccelerationStructure> TLAS;
    REQUIRE_CALL(device->createAccelerationStructure(createDesc, TLAS.writeRef()));

    queue = device->getQueue(QueueType::Graphics);
    enc = queue->createCommandEncoder();
    enc->buildAccelerationStructure(tlasBuildDesc, TLAS, nullptr, tlasScratch, 0, nullptr);
    queue->submit(enc->finish());
    queue->waitOnHost();

    // Output texture
    const uint32_t width = 256, height = 256;
    TextureDesc texDesc = {};
    texDesc.type = TextureType::Texture2D;
    texDesc.format = Format::RGBA32Float;
    texDesc.size.width = width;
    texDesc.size.height = height;
    texDesc.usage = TextureUsage::UnorderedAccess;
    ComPtr<ITexture> resultTexture;
    REQUIRE_CALL(device->createTexture(texDesc, nullptr, resultTexture.writeRef()));

    // Ray tracing pipeline
    ComPtr<slang::ISession> slangSession = device->getSlangSession();
    ComPtr<slang::IBlob> diagnosticsBlob;
    slang::IModule* module = slangSession->loadModule("test-ray-tracing-clusters", diagnosticsBlob.writeRef());
    diagnoseIfNeeded(diagnosticsBlob);
    REQUIRE(module != nullptr);
    std::vector<slang::IComponentType*> components;
    constexpr const char* kRayGen = "rayGenClusters";
    constexpr const char* kMiss = "missClusters";
    constexpr const char* kClosestHit = "closestHitClusters";
    constexpr const char* kHitGroup = "hit_group";
    components.push_back(module);
    ComPtr<slang::IEntryPoint> ep;
    REQUIRE_CALL(module->findEntryPointByName(kRayGen, ep.writeRef())); components.push_back(ep);
    REQUIRE_CALL(module->findEntryPointByName(kMiss, ep.writeRef())); components.push_back(ep);
    REQUIRE_CALL(module->findEntryPointByName(kClosestHit, ep.writeRef())); components.push_back(ep);
    ComPtr<slang::IComponentType> composedProgram;
    REQUIRE_CALL(slangSession->createCompositeComponentType(components.data(), (SlangInt)components.size(), composedProgram.writeRef(), diagnosticsBlob.writeRef()));
    diagnoseIfNeeded(diagnosticsBlob);
    ComPtr<slang::IComponentType> linkedProgram;
    REQUIRE_CALL(composedProgram->link(linkedProgram.writeRef(), diagnosticsBlob.writeRef()));
    diagnoseIfNeeded(diagnosticsBlob);
    ComPtr<IShaderProgram> rayTracingProgram = device->createShaderProgram(linkedProgram, diagnosticsBlob.writeRef());
    diagnoseIfNeeded(diagnosticsBlob);
    REQUIRE(rayTracingProgram != nullptr);

    RayTracingPipelineDesc rtp = {};
    rtp.program = rayTracingProgram;
    rtp.hitGroupCount = 1;
    HitGroupDesc hg = {};
    hg.closestHitEntryPoint = kClosestHit;
    hg.hitGroupName = kHitGroup;
    rtp.hitGroups = &hg;
    rtp.maxRayPayloadSize = 16;
    rtp.maxAttributeSizeInBytes = 8;
    rtp.maxRecursion = 1;
    rtp.flags = RayTracingPipelineFlags::EnableClusters;
    ComPtr<IRayTracingPipeline> pipeline;
    REQUIRE_CALL(device->createRayTracingPipeline(rtp, pipeline.writeRef()));

    const char* raygenNames[] = {kRayGen};
    const char* missNames[] = {kMiss};
    const char* hitGroupNames[] = {kHitGroup};
    ShaderTableDesc stDesc = {};
    stDesc.program = rayTracingProgram;
    stDesc.rayGenShaderCount = 1;
    stDesc.rayGenShaderEntryPointNames = raygenNames;
    stDesc.missShaderCount = 1;
    stDesc.missShaderEntryPointNames = missNames;
    stDesc.hitGroupCount = 1;
    stDesc.hitGroupNames = hitGroupNames;
    ComPtr<IShaderTable> shaderTable;
    REQUIRE_CALL(device->createShaderTable(stDesc, shaderTable.writeRef()));

    // Dispatch rays
    ComPtr<IBuffer> idsBuf;
    queue = device->getQueue(QueueType::Graphics);
    enc = queue->createCommandEncoder();
    {
        auto pass = enc->beginRayTracingPass();
        auto root = pass->bindPipeline(pipeline, shaderTable);
        ShaderCursor cursor(root);
        cursor["resultTexture"].setBinding(resultTexture);
        cursor["tlas"].setBinding(TLAS);
        BufferDesc idsDesc = {};
        idsDesc.size = uint64_t(width) * uint64_t(height) * sizeof(uint32_t) * 2;
        idsDesc.usage = BufferUsage::UnorderedAccess;
        idsDesc.defaultState = ResourceState::UnorderedAccess;
        idsBuf = device->createBuffer(idsDesc);
        cursor["ids_buffer"].setBinding(idsBuf);
        // ids are written at every pixel in closest hit
        pass->dispatchRays(0, width, height, 1);
        pass->end();
    }
    queue->submit(enc->finish());

    // Validate ClusterIDs for CUDA (OptiX). Other backends may differ; skip check there.
    if (device->getDeviceType() == DeviceType::CUDA)
    {
        // Map (stripIndex, cellCoord) -> linear index into the ids image buffer.
        // - stripIndex: 0 = lower strip center (y=0), 1 = upper strip center (y=kStripHeight+kStripGap)
        // - cellCoord: cellIndex + uWithinCell, e.g., 1.6 means cell 1 at 60% across
        auto makeProbeIndex = [&](uint32_t stripIndex, float cellCoord) -> uint64_t {
            // World X in [kStripXMin, kStripXMin + kStripXSpan]
            float xWorld = kStripXMin + (cellCoord / float(kGridW)) * kStripXSpan;
            float xUv = (xWorld + 1.0f) * 0.5f;
            uint32_t col = (uint32_t)(xUv * float(width - 1) + 0.5f);
            if (col >= width) col = width - 1;

            // World Y at the vertical center of the chosen strip
            float yWorld = (stripIndex == 0) ? 0.0f : (kStripHeight + kStripGap);
            float yUv = (yWorld + 1.0f) * 0.5f;
            uint32_t row = (uint32_t)(yUv * float(height - 1) + 0.5f);
            if (row >= height) row = height - 1;

            return uint64_t(row) * uint64_t(width) + uint64_t(col);
        };
        struct U2 { uint32_t x, y; } id0 = {}, id1 = {};
        uint64_t idx0 = makeProbeIndex(0, 1.6f);
        uint64_t idx1 = makeProbeIndex(1, 1.6f);
        CHECK_CALL(device->readBuffer(idsBuf, idx0 * sizeof(U2), sizeof(U2), &id0));
        CHECK_CALL(device->readBuffer(idsBuf, idx1 * sizeof(U2), sizeof(U2), &id1));

        CHECK_EQ(id0.x, 0u);
        CHECK_EQ(id1.x, 1u);
        
        // PrimitiveID resets per cluster. For cell c=1 and tri0, expected primitive index is 2*c = 2
        CHECK_EQ(id0.y, 2u);
        CHECK_EQ(id1.y, 2u);
    }

    // Read back and validate two horizontal hit strips (rows with any non-zero pixel)
    ComPtr<ISlangBlob> image;
    SubresourceLayout layout;
    REQUIRE_CALL(device->readTexture(resultTexture, 0, 0, image.writeRef(), &layout));
    const uint8_t* base = (const uint8_t*)image->getBufferPointer();
    auto isRowHit = [&](uint32_t y) {
        const uint8_t* row = base + y * layout.rowPitch;
        for (uint32_t x = 0; x < width; ++x)
        {
            const float* px = (const float*)(row + x * layout.colPitch);
            if (px[0] != 0.0f || px[1] != 0.0f || px[2] != 0.0f)
                return true;
        }
        return false;
    };
  
    constexpr uint32_t kStripMinRows = 2;
    uint32_t stripCount = 0;
    uint32_t stripHeight = 0;
    for (uint32_t y = 0; y < height; ++y)
    {
        bool hit = isRowHit(y);
        if (hit) { stripHeight++; }
        if ((!hit || y == height - 1) && stripHeight > 0)
        {
            if (stripHeight >= kStripMinRows)
                stripCount++;
            stripHeight = 0;
        }
    }
    CHECK_EQ(stripCount, 2u);
}
