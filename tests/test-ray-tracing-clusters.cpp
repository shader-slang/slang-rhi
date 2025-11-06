#include "testing.h"
#include <slang-rhi/acceleration-structure-utils.h>
#include <slang-rhi/cluster_accel_abi_host.h>

using namespace rhi;
using namespace rhi::testing;

struct Float3 { float x, y, z; };

static void requireClusterAccelOrSkip(IDevice* device)
{
    if (device->getDeviceType() != DeviceType::CUDA)
        SKIP("CUDA only test");
    if (device->getInfo().optixVersion < 90000)
        SKIP("requires OptiX 9+");
    if (!device->hasFeature(Feature::ClusterAccelerationStructure))
        SKIP("cluster acceleration structure not supported");
}

static uint64_t alignUp(uint64_t v, uint64_t a)
{
    return (v + (a - 1)) & ~(a - 1);
}

// Helper to create buffers commonly used in cluster accel tests.
static ComPtr<IBuffer> createAccelInputBuffer(IDevice* device, size_t size, const void* data = nullptr)
{
    BufferDesc desc = {};
    desc.size = size;
    desc.usage = BufferUsage::AccelerationStructureBuildInput;
    desc.defaultState = ResourceState::AccelerationStructureBuildInput;
    return device->createBuffer(desc, data);
}

static ComPtr<IBuffer> createUAVBuffer(IDevice* device, size_t size, const void* data = nullptr)
{
    BufferDesc desc = {};
    desc.size = size;
    desc.usage = BufferUsage::UnorderedAccess;
    desc.defaultState = ResourceState::UnorderedAccess;
    return device->createBuffer(desc, data);
}

static ComPtr<IBuffer> createAccelStructureBuffer(IDevice* device, size_t size)
{
    BufferDesc desc = {};
    desc.size = size;
    desc.usage = BufferUsage::AccelerationStructure;
    desc.defaultState = ResourceState::AccelerationStructure;
    return device->createBuffer(desc);
}

// Helper to create a simple TrianglesArgs with common defaults (no OMM, no primitive info, etc).
static cluster_abi::TrianglesArgs makeSimpleTrianglesArgs(
    uint32_t clusterId,
    uint32_t triangleCount,
    uint32_t vertexCount,
    uint64_t indexBuffer,
    uint64_t vertexBuffer,
    uint32_t vertexStrideBytes = sizeof(Float3))
{
    return cluster_abi::makeTrianglesArgs(
        clusterId,
        triangleCount,
        vertexCount,
        indexBuffer,
        vertexBuffer,
        vertexStrideBytes,
        /*indexFormat*/4  // 32-bit indices
    );
}

// Result of building a cluster acceleration structure in implicit mode.
struct ClasImplicitBuildResult
{
    ComPtr<IBuffer> outputBuffer;
    std::vector<uint64_t> handles;
    uint64_t handlesOffset = 0;
    uint64_t dataOffset = 0;
};

// Build CLAS in implicit mode: allocate buffers, build, read back handles.
// Takes a partially-filled ClusterAccelBuildDesc (op, argsBuffer, argsStride, argCount, limits).
// Returns the output buffer and handle values.
static ClasImplicitBuildResult buildClasImplicit(
    IDevice* device,
    ICommandQueue* queue,
    ClusterAccelBuildDesc buildDesc,
    uint32_t handleCount)
{
    ClasImplicitBuildResult result;

    ClusterAccelSizes sizes = {};
    REQUIRE_CALL(device->getClusterAccelerationStructureSizes(buildDesc, &sizes));

    // Allocate output buffer: handles region (aligned) + data region
    uint64_t handlesBytes = alignUp(
        uint64_t(handleCount) * uint64_t(cluster_abi::CLUSTER_HANDLE_BYTE_STRIDE),
        uint64_t(cluster_abi::CLUSTER_OUTPUT_ALIGNMENT));
    result.outputBuffer = createAccelStructureBuffer(device, handlesBytes + sizes.resultSize);
    ComPtr<IBuffer> scratch = createUAVBuffer(device, sizes.scratchSize);

    result.handlesOffset = 0;
    result.dataOffset = handlesBytes;

    buildDesc.mode = ClusterAccelBuildDesc::BuildMode::Implicit;
    buildDesc.modeDesc.implicit.outputHandlesBuffer = (DeviceAddress)result.outputBuffer->getDeviceAddress();
    buildDesc.modeDesc.implicit.outputHandlesStrideInBytes = 0;
    buildDesc.modeDesc.implicit.outputBuffer = (DeviceAddress)result.outputBuffer->getDeviceAddress() + handlesBytes;
    buildDesc.modeDesc.implicit.outputBufferSizeInBytes = handlesBytes + sizes.resultSize;
    buildDesc.modeDesc.implicit.tempBuffer = (DeviceAddress)scratch->getDeviceAddress();
    buildDesc.modeDesc.implicit.tempBufferSizeInBytes = sizes.scratchSize;

    auto enc = queue->createCommandEncoder();
    enc->buildClusterAccelerationStructure(buildDesc);
    queue->submit(enc->finish());
    queue->waitOnHost();

    result.handles.resize(handleCount);
    REQUIRE_CALL(device->readBuffer(
        result.outputBuffer,
        result.handlesOffset,
        handleCount * sizeof(uint64_t),
        result.handles.data()));

    return result;
}

// Result of building a BLAS from CLAS handles in implicit mode.
struct BlasFromClasResult
{
    ComPtr<IBuffer> outputBuffer;
    uint64_t blasHandle = 0;
};

// Build BLAS from CLAS handles in implicit mode.
// Creates the args buffer internally and returns the BLAS output buffer and handle.
static BlasFromClasResult buildBlasFromClasImplicit(
    IDevice* device,
    ICommandQueue* queue,
    DeviceAddress clusterHandlesBuffer,
    uint32_t clusterCount)
{
    BlasFromClasResult result;

    cluster_abi::ClustersArgs clustersArgs = cluster_abi::makeClustersArgs(
        clusterCount,
        clusterHandlesBuffer,
        cluster_abi::CLUSTER_HANDLE_BYTE_STRIDE
    );

    ComPtr<IBuffer> argsBuffer = createAccelInputBuffer(device, sizeof(clustersArgs), &clustersArgs);

    ClusterAccelBuildDesc blasDesc = {};
    blasDesc.op = ClusterAccelBuildOp::BLASFromCLAS;
    blasDesc.argsBuffer = BufferOffsetPair(argsBuffer, 0);
    blasDesc.argsStride = sizeof(cluster_abi::ClustersArgs);
    blasDesc.argCount = 1;
    blasDesc.limits.limitsClusters.maxArgCount = 1;
    blasDesc.limits.limitsClusters.maxTotalClusterCount = clusterCount;
    blasDesc.limits.limitsClusters.maxClusterCountPerArg = clusterCount;

    ClusterAccelSizes sizes = {};
    REQUIRE_CALL(device->getClusterAccelerationStructureSizes(blasDesc, &sizes));

    uint64_t handlesBytes = alignUp(
        uint64_t(cluster_abi::CLUSTER_HANDLE_BYTE_STRIDE),
        uint64_t(cluster_abi::CLUSTER_OUTPUT_ALIGNMENT));
    result.outputBuffer = createAccelStructureBuffer(device, handlesBytes + sizes.resultSize);
    ComPtr<IBuffer> scratch = createUAVBuffer(device, sizes.scratchSize);

    blasDesc.mode = ClusterAccelBuildDesc::BuildMode::Implicit;
    blasDesc.modeDesc.implicit.outputHandlesBuffer = (DeviceAddress)result.outputBuffer->getDeviceAddress();
    blasDesc.modeDesc.implicit.outputHandlesStrideInBytes = 0;
    blasDesc.modeDesc.implicit.outputBuffer = (DeviceAddress)result.outputBuffer->getDeviceAddress() + handlesBytes;
    blasDesc.modeDesc.implicit.outputBufferSizeInBytes = handlesBytes + sizes.resultSize;
    blasDesc.modeDesc.implicit.tempBuffer = (DeviceAddress)scratch->getDeviceAddress();
    blasDesc.modeDesc.implicit.tempBufferSizeInBytes = sizes.scratchSize;

    auto enc = queue->createCommandEncoder();
    enc->buildClusterAccelerationStructure(blasDesc);
    queue->submit(enc->finish());
    queue->waitOnHost();

    REQUIRE_CALL(device->readBuffer(result.outputBuffer, 0, sizeof(uint64_t), &result.blasHandle));

    return result;
}

GPU_TEST_CASE("cluster-accel-sizes-optix", CUDA)
{
    requireClusterAccelOrSkip(device);

    ClusterAccelBuildDesc clasDesc = {};
    clasDesc.op = ClusterAccelBuildOp::CLASFromTriangles;
    clasDesc.limits.limitsTriangles.maxArgCount = 1;
    clasDesc.limits.limitsTriangles.maxTriangleCountPerArg = 1;
    clasDesc.limits.limitsTriangles.maxVertexCountPerArg = 3;
    clasDesc.limits.limitsTriangles.maxUniqueSbtIndexCountPerArg = 1;
    ClusterAccelSizes clasSizes = {};
    CHECK_CALL(device->getClusterAccelerationStructureSizes(clasDesc, &clasSizes));
    CHECK_GT(clasSizes.resultSize, 0);
    CHECK_GT(clasSizes.scratchSize, 0);

    ClusterAccelBuildDesc blasDesc = {};
    blasDesc.op = ClusterAccelBuildOp::BLASFromCLAS;
    blasDesc.limits.limitsClusters.maxArgCount = 1;
    blasDesc.limits.limitsClusters.maxTotalClusterCount = 1;
    blasDesc.limits.limitsClusters.maxClusterCountPerArg = 1;
    ClusterAccelSizes blasSizes = {};
    CHECK_CALL(device->getClusterAccelerationStructureSizes(blasDesc, &blasSizes));
    CHECK_GT(blasSizes.resultSize, 0);
    CHECK_GT(blasSizes.scratchSize, 0);
}

GPU_TEST_CASE("cluster-accel-build-one-triangle", CUDA)
{
    requireClusterAccelOrSkip(device);

    const Float3 vertices[3] = {{0.f,0.f,0.f},{1.f,0.f,0.f},{0.f,1.f,0.f}};
    const uint32_t indices[3] = {0,1,2};

    ComPtr<IBuffer> vbuf = createAccelInputBuffer(device, sizeof(vertices), vertices);
    ComPtr<IBuffer> ibuf = createAccelInputBuffer(device, sizeof(indices), indices);

    cluster_abi::TrianglesArgs triArgs = makeSimpleTrianglesArgs(
        0, 1, 3, ibuf->getDeviceAddress(), vbuf->getDeviceAddress());
    ComPtr<IBuffer> args = createAccelInputBuffer(device, sizeof(triArgs), &triArgs);

    ClusterAccelBuildDesc clasDesc = {};
    clasDesc.op = ClusterAccelBuildOp::CLASFromTriangles;
    clasDesc.argsBuffer = BufferOffsetPair(args, 0);
    clasDesc.argsStride = sizeof(cluster_abi::TrianglesArgs);
    clasDesc.argCount = 1;
    clasDesc.limits.limitsTriangles.maxArgCount = 1;
    clasDesc.limits.limitsTriangles.maxTriangleCountPerArg = 1;
    clasDesc.limits.limitsTriangles.maxVertexCountPerArg = 3;
    clasDesc.limits.limitsTriangles.maxUniqueSbtIndexCountPerArg = 1;

    auto queue = device->getQueue(QueueType::Graphics);
    ClasImplicitBuildResult clasResult = buildClasImplicit(device, queue, clasDesc, 1);
    CHECK_NE(clasResult.handles[0], 0);

    BlasFromClasResult blasResult = buildBlasFromClasImplicit(
        device, queue, clasResult.outputBuffer->getDeviceAddress(), 1);
    CHECK_NE(blasResult.blasHandle, 0);
}

GPU_TEST_CASE("cluster-accel-batch-two-clusters", CUDA)
{
    requireClusterAccelOrSkip(device);

    const Float3 vertices[6] = {{0,0,0},{1,0,0},{0,1,0}, {2,0,0},{3,0,0},{2,1,0}};
    const uint32_t indices[6] = {0,1,2, 0,1,2};

    ComPtr<IBuffer> vbuf = createAccelInputBuffer(device, sizeof(vertices), vertices);
    ComPtr<IBuffer> ibuf = createAccelInputBuffer(device, sizeof(indices), indices);

    cluster_abi::TrianglesArgs triArgs[2];
    for (int i = 0; i < 2; i++)
    {
        triArgs[i] = makeSimpleTrianglesArgs(
            i, 1, 3,
            ibuf->getDeviceAddress() + (i * 3 * sizeof(uint32_t)),
            vbuf->getDeviceAddress() + (i * 3 * sizeof(Float3)));
    }

    ComPtr<IBuffer> args = createAccelInputBuffer(device, sizeof(triArgs), &triArgs[0]);

    ClusterAccelBuildDesc clasDesc = {};
    clasDesc.op = ClusterAccelBuildOp::CLASFromTriangles;
    clasDesc.argsBuffer = BufferOffsetPair(args, 0);
    clasDesc.argsStride = sizeof(cluster_abi::TrianglesArgs);
    clasDesc.argCount = 2;
    clasDesc.limits.limitsTriangles.maxArgCount = 2;
    clasDesc.limits.limitsTriangles.maxTriangleCountPerArg = 1;
    clasDesc.limits.limitsTriangles.maxVertexCountPerArg = 3;
    clasDesc.limits.limitsTriangles.maxUniqueSbtIndexCountPerArg = 1;

    auto queue = device->getQueue(QueueType::Graphics);
    ClasImplicitBuildResult clasResult = buildClasImplicit(device, queue, clasDesc, 2);
    CHECK_NE(clasResult.handles[0], 0);
    CHECK_NE(clasResult.handles[1], 0);

    BlasFromClasResult blasResult = buildBlasFromClasImplicit(
        device, queue, clasResult.outputBuffer->getDeviceAddress(), 2);
    CHECK_NE(blasResult.blasHandle, 0);
}


GPU_TEST_CASE("cluster-accel-explicit-two-clusters", CUDA)
{
    requireClusterAccelOrSkip(device);

    const Float3 vertices[6] = {{0,0,0},{1,0,0},{0,1,0}, {2,0,0},{3,0,0},{2,1,0}};
    const uint32_t indices[6] = {0,1,2, 0,1,2};

    ComPtr<IBuffer> vbuf = createAccelInputBuffer(device, sizeof(vertices), vertices);
    ComPtr<IBuffer> ibuf = createAccelInputBuffer(device, sizeof(indices), indices);

    cluster_abi::TrianglesArgs triArgs[2];
    for (int i = 0; i < 2; i++)
    {
        triArgs[i] = makeSimpleTrianglesArgs(
            i, 1, 3,
            ibuf->getDeviceAddress() + (i * 3 * sizeof(uint32_t)),
            vbuf->getDeviceAddress() + (i * 3 * sizeof(Float3)));
    }

    ComPtr<IBuffer> args = createAccelInputBuffer(device, sizeof(triArgs), &triArgs[0]);

    ClusterAccelBuildDesc clasDesc = {};
    clasDesc.op = ClusterAccelBuildOp::CLASFromTriangles;
    clasDesc.argsBuffer = BufferOffsetPair(args, 0);
    clasDesc.argsStride = sizeof(cluster_abi::TrianglesArgs);
    clasDesc.argCount = 2;
    clasDesc.limits.limitsTriangles.maxArgCount = 2;
    clasDesc.limits.limitsTriangles.maxTriangleCountPerArg = 1;
    clasDesc.limits.limitsTriangles.maxVertexCountPerArg = 3;
    clasDesc.limits.limitsTriangles.maxUniqueSbtIndexCountPerArg = 1;

    // Scratch sizing (used for both get-sizes and explicit build)
    ClusterAccelSizes clasSizes = {};
    CHECK_CALL(device->getClusterAccelerationStructureSizes(clasDesc, &clasSizes));

    ComPtr<IBuffer> clasScratch = createUAVBuffer(device, clasSizes.scratchSize);

    // Step 1: GET_SIZES to produce per-CLAS sizes
    uint32_t zeroSizes[2] = {0,0};
    ComPtr<IBuffer> sizesBuf = createUAVBuffer(device, sizeof(uint32_t) * 2, zeroSizes);

    clasDesc.mode = ClusterAccelBuildDesc::BuildMode::GetSizes;
    clasDesc.modeDesc.getSizes.outputSizesBuffer = sizesBuf->getDeviceAddress();
    clasDesc.modeDesc.getSizes.outputSizesStrideInBytes = 0; // 0 -> 4
    // Required temp for GetSizes
    clasDesc.modeDesc.getSizes.tempBuffer = clasScratch->getDeviceAddress();
    clasDesc.modeDesc.getSizes.tempBufferSizeInBytes = clasSizes.scratchSize;

    // Build with GET_SIZES; result buffer unused, provide a small dummy
    ComPtr<IBuffer> dummyOut = createAccelStructureBuffer(device, 256);

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
    uint64_t off0 = 0;
    uint64_t off1 = alignUp(uint64_t(sizesHost[0]), (uint64_t)cluster_abi::CLUSTER_OUTPUT_ALIGNMENT);
    uint64_t totalArena = off1 + alignUp(uint64_t(sizesHost[1]), (uint64_t)cluster_abi::CLUSTER_OUTPUT_ALIGNMENT);

    ComPtr<IBuffer> arena = createAccelStructureBuffer(device, totalArena);

    uint64_t destAddrsHost[2] = {
        arena->getDeviceAddress() + off0,
        arena->getDeviceAddress() + off1,
    };
    ComPtr<IBuffer> destAddrBuf = createUAVBuffer(device, sizeof(destAddrsHost), destAddrsHost);

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

    ComPtr<IBuffer> vbuf = createAccelInputBuffer(device, vertices.size() * sizeof(Float3), vertices.data());
    ComPtr<IBuffer> ibuf = createAccelInputBuffer(device, indices.size() * sizeof(uint32_t), indices.data());

    // Build flow: TemplatesFromTriangles (1) -> CLASFromTemplates (2 instances)
    const uint32_t clusterCount = 2;

    // Create one template from triangles (host-filled args)
    cluster_abi::TrianglesArgs trianglesArgs = makeSimpleTrianglesArgs(
        /*clusterId*/0,
        /*triangleCount*/triPerCluster,
        /*vertexCount*/vtxPerCluster,
        /*indexBuffer*/ibuf->getDeviceAddress(),
        /*vertexBuffer*/vbuf->getDeviceAddress(),
        /*vertexStrideBytes*/(uint32_t)sizeof(Float3));
    ComPtr<IBuffer> trianglesArgsBuffer = createAccelInputBuffer(device, sizeof(trianglesArgs), &trianglesArgs);

    ClusterAccelBuildDesc templatesFromTrianglesDesc = {};
    templatesFromTrianglesDesc.op = ClusterAccelBuildOp::TemplatesFromTriangles;
    templatesFromTrianglesDesc.argsBuffer = BufferOffsetPair(trianglesArgsBuffer, 0);
    templatesFromTrianglesDesc.argsStride = sizeof(cluster_abi::TrianglesArgs);
    templatesFromTrianglesDesc.argCount = 1;
    templatesFromTrianglesDesc.limits.limitsTriangles.maxArgCount = 1;
    templatesFromTrianglesDesc.limits.limitsTriangles.maxTriangleCountPerArg = triPerCluster;
    templatesFromTrianglesDesc.limits.limitsTriangles.maxVertexCountPerArg = vtxPerCluster;
    templatesFromTrianglesDesc.limits.limitsTriangles.maxUniqueSbtIndexCountPerArg = 1;

    auto queue = device->getQueue(QueueType::Graphics);
    ClasImplicitBuildResult templatesImplicitResult = buildClasImplicit(device, queue, templatesFromTrianglesDesc, /*handleCount*/1);
    CHECK_NE(templatesImplicitResult.handles[0], 0);

    // Instantiate the template twice via CLASFromTemplates
    cluster_abi::TemplatesArgs templateInstantiationArgs[2] = {};
    uint64_t templateHandle = templatesImplicitResult.handles[0];
    uint64_t vertexBaseAddress = vbuf->getDeviceAddress();
    uint32_t vertexStrideInBytes = (uint32_t)sizeof(Float3);
    // Note: For a large number of clusters, we could populate these args with a compute
    // kernel. For two clusters, host-side filling is ok.
    for (uint32_t i = 0; i < clusterCount; ++i)
    {
        uint64_t vertexAddress = vertexBaseAddress + uint64_t(i) * uint64_t(vtxPerCluster) * uint64_t(vertexStrideInBytes);
        templateInstantiationArgs[i] = cluster_abi::makeTemplatesArgs(
            /*clusterTemplate*/templateHandle,
            /*vertexBuffer*/vertexAddress,
            /*vertexStrideInBytes*/vertexStrideInBytes,
            /*clusterIdOffset*/i,
            /*sbtIndexOffset*/0);
    }
    ComPtr<IBuffer> templateInstantiationArgsBuffer = createAccelInputBuffer(device, sizeof(templateInstantiationArgs), &templateInstantiationArgs[0]);

    ClusterAccelBuildDesc clasFromTemplates = {};
    clasFromTemplates.op = ClusterAccelBuildOp::CLASFromTemplates;
    clasFromTemplates.argsBuffer = BufferOffsetPair(templateInstantiationArgsBuffer, 0);
    clasFromTemplates.argsStride = (uint32_t)sizeof(cluster_abi::TemplatesArgs);
    clasFromTemplates.argCount = clusterCount;
    // Reuse triangle-related limits (per API contract)
    clasFromTemplates.limits.limitsTriangles.maxArgCount = clusterCount;
    clasFromTemplates.limits.limitsTriangles.maxTriangleCountPerArg = triPerCluster;
    clasFromTemplates.limits.limitsTriangles.maxVertexCountPerArg = vtxPerCluster;
    clasFromTemplates.limits.limitsTriangles.maxUniqueSbtIndexCountPerArg = 1;

    ClasImplicitBuildResult clasResult = buildClasImplicit(device, queue, clasFromTemplates, clusterCount);
    CHECK_NE(clasResult.handles[0], 0);
    CHECK_NE(clasResult.handles[1], 0);

    // Build BLAS from CLAS handles
    BlasFromClasResult blasResult = buildBlasFromClasImplicit(
        device, queue, clasResult.outputBuffer->getDeviceAddress(), clusterCount);
    CHECK_NE(blasResult.blasHandle, 0);

    // Build TLAS from BLAS handle
    AccelerationStructureInstanceDescType nativeInstanceDescType = getAccelerationStructureInstanceDescType(device);
    Size nativeInstanceDescSize = getAccelerationStructureInstanceDescSize(nativeInstanceDescType);
    std::vector<AccelerationStructureInstanceDescGeneric> genericInstanceDescs(1);
    float transformMatrix[] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    memcpy(&genericInstanceDescs[0].transform[0][0], transformMatrix, sizeof(float) * 12);
    genericInstanceDescs[0].instanceID = 0;
    genericInstanceDescs[0].instanceMask = 0xff;
    genericInstanceDescs[0].instanceContributionToHitGroupIndex = 0;
    genericInstanceDescs[0].accelerationStructure = AccelerationStructureHandle{blasResult.blasHandle};

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
    auto enc = queue->createCommandEncoder();
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
