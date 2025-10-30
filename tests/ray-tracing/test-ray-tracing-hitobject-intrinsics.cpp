#include "testing.h"
#include "texture-utils.h"

#include <slang-rhi/acceleration-structure-utils.h>

using namespace rhi;
using namespace rhi::testing;

// Vertex and index data for a single triangle. We don't actually use it, but slang-rhi disallows non-empty accel
// structures.

struct SingleTriangleBlas {
    struct Vertex
    {
        float position[3];
    };

    static const int kVertexCount = 3;
    inline static const Vertex kVertexData[kVertexCount] = {
        {0.f, 0.f, 1.f},
        {1.f, 0.f, 1.f},
        {0.f, 1.f, 1.f},
    };

    static const int kIndexCount = 3;
    inline static const uint32_t kIndexData[kIndexCount] = {0, 1, 2};

    ComPtr<IBuffer> vertexBuffer;
    ComPtr<IBuffer> indexBuffer;

    ComPtr<IBuffer> BLASBuffer;
    ComPtr<IAccelerationStructure> BLAS;

    SingleTriangleBlas(IDevice* device, ICommandQueue* queue) {
        BufferDesc vertexBufferDesc;
        vertexBufferDesc.size = kVertexCount * sizeof(Vertex);
        vertexBufferDesc.usage = BufferUsage::AccelerationStructureBuildInput;
        vertexBufferDesc.defaultState = ResourceState::AccelerationStructureBuildInput;
        vertexBuffer = device->createBuffer(vertexBufferDesc, &kVertexData[0]);
        REQUIRE(vertexBuffer != nullptr);

        BufferDesc indexBufferDesc;
        indexBufferDesc.size = kIndexCount * sizeof(int32_t);
        indexBufferDesc.usage = BufferUsage::AccelerationStructureBuildInput;
        indexBufferDesc.defaultState = ResourceState::AccelerationStructureBuildInput;
        indexBuffer = device->createBuffer(indexBufferDesc, &kIndexData[0]);
        REQUIRE(indexBuffer != nullptr);

        AccelerationStructureBuildInput buildInput = {};
        buildInput.type = AccelerationStructureBuildInputType::Triangles;
        buildInput.triangles.vertexBuffers[0] = vertexBuffer;
        buildInput.triangles.vertexBufferCount = 1;
        buildInput.triangles.vertexFormat = Format::RGB32Float;
        buildInput.triangles.vertexCount = kVertexCount;
        buildInput.triangles.vertexStride = sizeof(Vertex);
        buildInput.triangles.indexBuffer = indexBuffer;
        buildInput.triangles.indexFormat = IndexFormat::Uint32;
        buildInput.triangles.indexCount = kIndexCount;
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

struct SingleCustomGeometryBlas {
static const int kAabbCount = 1;
inline static const AccelerationStructureAABB kAabbData[kAabbCount] = {
    {-0.5f, -0.5f, 0.5f, 0.5f, 0.5f, 1.0f},
};

    ComPtr<IBuffer> aabbBuffer;

    ComPtr<IBuffer> BLASBuffer;
    ComPtr<IAccelerationStructure> BLAS;

    SingleCustomGeometryBlas(IDevice* device, ICommandQueue* queue) {
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

struct Tlas {
    ComPtr<IBuffer> instanceBuffer;
    ComPtr<IBuffer> TLASBuffer;
    ComPtr<IAccelerationStructure> TLAS;

    Tlas(IDevice* device, ICommandQueue* queue, IAccelerationStructure* BLAS) {
        AccelerationStructureInstanceDescType nativeInstanceDescType =
            getAccelerationStructureInstanceDescType(device);
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

Result loadShaderPrograms( IDevice* device, const char* moduleName, const std::vector<const char*>& programNames, IShaderProgram** outProgram )
{
    ComPtr<slang::ISession> slangSession;
    slangSession = device->getSlangSession();

    ComPtr<slang::IBlob> diagnosticsBlob;
    slang::IModule* module = slangSession->loadModule(moduleName, diagnosticsBlob.writeRef());
    diagnoseIfNeeded(diagnosticsBlob);
    if (!module)
        return SLANG_FAIL;

    std::vector<slang::IComponentType*> componentTypes;
    componentTypes.push_back(module);

    ComPtr<slang::IEntryPoint> entryPoint;

    for( const char* programName : programNames )
    {
        SLANG_RETURN_ON_FAIL(module->findEntryPointByName(programName, entryPoint.writeRef()));
        componentTypes.push_back(entryPoint);
    }

    ComPtr<slang::IComponentType> linkedProgram;
    Result result = slangSession->createCompositeComponentType(
        componentTypes.data(),
        componentTypes.size(),
        linkedProgram.writeRef(),
        diagnosticsBlob.writeRef()
    );
    SLANG_RETURN_ON_FAIL(result);

    ShaderProgramDesc programDesc = {};
    programDesc.slangGlobalScope = linkedProgram;
    SLANG_RETURN_ON_FAIL(device->createShaderProgram(programDesc, outProgram));

    return SLANG_OK;
}

static const char* defaultClosestHit = "closestHitNOP";
static const char* defaultMiss = "missNOP";

struct HitGroupProgramNames
{
    const char* closesthit = defaultClosestHit;
    const char* intersection = nullptr;
};

struct RayTracingTestPipeline
{
    ComPtr<IRayTracingPipeline> raytracingPipeline;
    ComPtr<IShaderTable> shaderTable;

    RayTracingTestPipeline(
        IDevice* device,
        const char* filepath,
        const char* raygenName,
        const std::vector<HitGroupProgramNames>& programNames,
        const std::vector<const char*>& missNames
    )
    {
        ComPtr<IShaderProgram> rayTracingProgram;

        REQUIRE( raygenName != nullptr);
        REQUIRE( programNames.size() > 0);
        REQUIRE( missNames.size() > 0);

        std::vector<const char*> programsToLoad = {raygenName};

        for( const HitGroupProgramNames& programName : programNames)
        {
            programsToLoad.push_back(programName.closesthit);
            if (programName.intersection)
                programsToLoad.push_back(programName.intersection);
        }

        for( const char* missName : missNames)
            programsToLoad.push_back(missName);

        REQUIRE_CALL(loadShaderPrograms(
            device,
            filepath,
            programsToLoad,
            rayTracingProgram.writeRef()
        ));

        std::vector<std::string> hitgroupNames;
        for( unsigned int i = 0; i < programNames.size(); i++)
            hitgroupNames.push_back("hitgroup" + std::to_string(i + 1));

        std::vector<const char*> hitgroupNamesCstr;
        for( const std::string& hitgroupName : hitgroupNames)
            hitgroupNamesCstr.push_back(hitgroupName.c_str());

        std::vector<HitGroupDesc> hitGroups;

        for( unsigned int i = 0; i < programNames.size(); i++)
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
        REQUIRE_CALL(device->createRayTracingPipeline(rtpDesc, raytracingPipeline.writeRef()));
        REQUIRE(raytracingPipeline != nullptr);


        ShaderTableDesc shaderTableDesc = {};
        shaderTableDesc.program = rayTracingProgram;
        shaderTableDesc.hitGroupCount = hitgroupNames.size();
        shaderTableDesc.hitGroupNames = hitgroupNamesCstr.data();
        shaderTableDesc.rayGenShaderCount = 1;
        shaderTableDesc.rayGenShaderEntryPointNames = &raygenName;
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
)
{
    auto commandEncoder = queue->createCommandEncoder();

    auto passEncoder = commandEncoder->beginRayTracingPass();
    auto rootObject = passEncoder->bindPipeline(pipeline, shaderTable);
    auto cursor = ShaderCursor(rootObject);
    cursor["resultBuffer"].setBinding(resultBuffer);
    cursor["sceneBVH"].setBinding(TLAS);
    passEncoder->dispatchRays(0, 1, 1, 1);
    passEncoder->end();

    queue->submit(commandEncoder->finish());
    queue->waitOnHost();
}

struct TestResult
{
    int queryWasSuccess;
    int invokeWasSuccess;

    float rayOrigin[3];
    float rayDirection[3];
};

struct RayTracingSingleTriangleTest
{
    IDevice* device;

    void init(IDevice* device_) { this->device = device_; }

    ResultBuffer resultBuf;

    void createResultBuffer(size_t resultSize)
    {
        resultBuf = ResultBuffer(device, resultSize);
    }

    void run(
        const char* filepath,
        const char* raygenName,
        const std::vector<const char*>& closestHitNames,
        const std::vector<const char*>& missNames
    )
    {
        ComPtr<ICommandQueue> queue = device->getQueue(QueueType::Graphics);

        SingleTriangleBlas blas(device, queue);
        Tlas tlas(device, queue, blas.BLAS);

        std::vector<HitGroupProgramNames> hitGroupProgramNames;
        for( const char* closestHitName : closestHitNames)
            hitGroupProgramNames.push_back({closestHitName, /*intersection=*/nullptr});

        RayTracingTestPipeline pipeline(device, filepath, raygenName, hitGroupProgramNames, missNames);
        launchPipeline(queue, pipeline.raytracingPipeline, pipeline.shaderTable, resultBuf.resultBuffer, tlas.TLAS);
    }

    ComPtr<ISlangBlob> getTestResult()
    {
        ComPtr<ISlangBlob> resultBlob;
        resultBuf.getFromDevice(resultBlob.writeRef());
        return resultBlob;
    }
};

struct RayTracingSingleCustomGeometryTest
{
    IDevice* device;

    void init(IDevice* device_) { this->device = device_; }

    ResultBuffer resultBuf;

    void createResultBuffer(size_t resultSize)
    {
        resultBuf = ResultBuffer(device, resultSize);
    }

    void run(
        const char* filepath,
        const char* raygenName,
        const std::vector<HitGroupProgramNames>& hitGroupProgramNames,
        const std::vector<const char*>& missNames
    )
    {
        ComPtr<ICommandQueue> queue = device->getQueue(QueueType::Graphics);

        SingleCustomGeometryBlas blas(device, queue);
        Tlas tlas(device, queue, blas.BLAS);

        RayTracingTestPipeline pipeline(device, filepath, raygenName, hitGroupProgramNames, missNames);
        launchPipeline(queue, pipeline.raytracingPipeline, pipeline.shaderTable, resultBuf.resultBuffer, tlas.TLAS);

        ComPtr<ISlangBlob> resultBlob;
        resultBuf.getFromDevice(resultBlob.writeRef());
    }

    ComPtr<ISlangBlob> getTestResult()
    {
        ComPtr<ISlangBlob> resultBlob;
        resultBuf.getFromDevice(resultBlob.writeRef());
        return resultBlob;
    }
};

void checkQueryAndInvokeResult(ISlangBlob* resultBlob)
{
    const TestResult* testResult = reinterpret_cast<const TestResult*>(resultBlob->getBufferPointer());
    CHECK_EQ(testResult->queryWasSuccess, 1);
    CHECK_EQ(testResult->invokeWasSuccess, 1);
}

GPU_TEST_CASE("ray-tracing-hitobject-query-invoke-nop-rg", ALL)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    RayTracingSingleTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(TestResult));
    test.run(
        "ray-tracing/test-ray-tracing-hitobject-intrinsics",
        "rayGenShaderMakeQueryInvokeNOP",
        {"closestHitNOP"},
        {"missNOP"}
    );

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    checkQueryAndInvokeResult(resultBlob);
}

GPU_TEST_CASE("ray-tracing-hitobject-query-invoke-nop-ch", ALL)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    RayTracingSingleTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(TestResult));
    test.run(
        "ray-tracing/test-ray-tracing-hitobject-intrinsics",
        "rayGenShaderInvokeCH",
        {"closestHitMakeQueryInvokeNOP"},
        {"missNOP"}
    );

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    checkQueryAndInvokeResult(resultBlob);
}

GPU_TEST_CASE("ray-tracing-hitobject-query-invoke-nop-ms", ALL)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    RayTracingSingleTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(TestResult));
    test.run(
        "ray-tracing/test-ray-tracing-hitobject-intrinsics",
        "rayGenShaderInvokeMS",
        {"closestHitNOP"},
        {"missMakeQueryInvokeNOP"}
    );

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    checkQueryAndInvokeResult(resultBlob);
}

GPU_TEST_CASE("ray-tracing-hitobject-query-invoke-miss-rg", ALL)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    RayTracingSingleTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(TestResult));
    test.run(
        "ray-tracing/test-ray-tracing-hitobject-intrinsics",
        "rayGenShaderMakeQueryInvokeMiss",
        {"closestHitNOP"},
        {"missInvoke"}
    );

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    checkQueryAndInvokeResult(resultBlob);
}

GPU_TEST_CASE("ray-tracing-hitobject-query-invoke-miss-ch", ALL)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    RayTracingSingleTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(TestResult));
    test.run(
        "ray-tracing/test-ray-tracing-hitobject-intrinsics",
        "rayGenShaderInvokeCH",
        {"closestHitMakeQueryInvokeMiss"},
        {"missInvoke"}
    );

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    checkQueryAndInvokeResult(resultBlob);
}

GPU_TEST_CASE("ray-tracing-hitobject-query-invoke-miss-ms", ALL)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    RayTracingSingleTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(TestResult));
    test.run(
        "ray-tracing/test-ray-tracing-hitobject-intrinsics",
        "rayGenShaderInvokeMS",
        {"closestHitNOP"},
        {"missMakeQueryInvokeMiss", "missInvoke"}
    );

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    checkQueryAndInvokeResult(resultBlob);
}

GPU_TEST_CASE("ray-tracing-hitobject-query-invoke-hit-rg", ALL)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    RayTracingSingleTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(TestResult));
    test.run(
        "ray-tracing/test-ray-tracing-hitobject-intrinsics",
        "rayGenShaderTraceQueryInvokeHit",
        {"closestHitInvoke"},
        {"missNOP"}
    );

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    checkQueryAndInvokeResult(resultBlob);
}

GPU_TEST_CASE("ray-tracing-hitobject-query-invoke-hit-ch", ALL)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    RayTracingSingleTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(TestResult));
    test.run(
        "ray-tracing/test-ray-tracing-hitobject-intrinsics",
        "rayGenShaderInvokeCH",
        {"closestHitMakeQueryInvokeHit", "closestHitInvoke"},
        {"missNOP"}
    );

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    checkQueryAndInvokeResult(resultBlob);
}

GPU_TEST_CASE("ray-tracing-hitobject-query-invoke-hit-ms", ALL)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    RayTracingSingleTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(TestResult));
    test.run(
        "ray-tracing/test-ray-tracing-hitobject-intrinsics",
        "rayGenShaderInvokeMS",
        {"closestHitNOP", "closestHitInvoke"},
        {"missMakeQueryInvokeHit"}
    );

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    checkQueryAndInvokeResult(resultBlob);
}

// CUDA disabled due to https://github.com/shader-slang/slang/issues/8836
GPU_TEST_CASE("ray-tracing-hitobject-query-hit-kind-front-face", ALL & ~CUDA)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    RayTracingSingleTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(TestResult));
    test.run(
        "ray-tracing/test-ray-tracing-hitobject-intrinsics",
        "rayGenShaderQueryHitKindFrontFace",
        {"closestHitNOP"},
        {"missNOP"}
    );

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    checkQueryAndInvokeResult(resultBlob);
}

// CUDA disabled due to https://github.com/shader-slang/slang/issues/8836
GPU_TEST_CASE("ray-tracing-hitobject-query-hit-kind-back-face", ALL & ~CUDA)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    RayTracingSingleTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(TestResult));
    test.run(
        "ray-tracing/test-ray-tracing-hitobject-intrinsics",
        "rayGenShaderQueryHitKindBackFace",
        {"closestHitNOP"},
        {"missNOP"}
    );

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    checkQueryAndInvokeResult(resultBlob);
}

GPU_TEST_CASE("ray-tracing-hitobject-query-hit-kind-custom", ALL & ~CUDA)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    RayTracingSingleCustomGeometryTest test;
    test.init(device);
    test.createResultBuffer(sizeof(TestResult));
    test.run(
        "ray-tracing/test-ray-tracing-hitobject-intrinsics",
        "rayGenShaderQueryHitKindCustom",
        {{"closestHitNOP", "intersectionReportHitWithKind"}},
        {"missNOP"}
    );

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    checkQueryAndInvokeResult(resultBlob);
}

// CUDA/OptiX is disabled because it only supports getting the ray origin in world space.
// D3D12 is disabled due to https://github.com/shader-slang/slang/issues/8615
GPU_TEST_CASE("ray-tracing-hitobject-query-hit-ray-object-origin", ALL & ~CUDA & ~D3D12)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    RayTracingSingleTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(TestResult));
    test.run(
        "ray-tracing/test-ray-tracing-hitobject-intrinsics",
        "rayGenShaderQueryRayObjectOrigin",
        {"closestHitNOP"},
        {"missNOP"}
    );

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    const TestResult* result = reinterpret_cast<const TestResult*>(resultBlob->getBufferPointer());

    CHECK_EQ(result->rayOrigin[0], 0.1f);
    CHECK_EQ(result->rayOrigin[1], 0.1f);
    CHECK_EQ(result->rayOrigin[2], 0.1f);
}

// Disabled under CUDA/OptiX and D3D12 due to https://github.com/shader-slang/slang/issues/8615
GPU_TEST_CASE("ray-tracing-hitobject-query-hit-ray-object-direction", ALL & ~CUDA & ~D3D12)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    RayTracingSingleTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(TestResult));
    test.run(
        "ray-tracing/test-ray-tracing-hitobject-intrinsics",
        "rayGenShaderQueryRayObjectDirection",
        {"closestHitNOP"},
        {"missNOP"}
    );

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    const TestResult* result = reinterpret_cast<const TestResult*>(resultBlob->getBufferPointer());

    CHECK_EQ(result->rayDirection[0], 0.0f);
    CHECK_EQ(result->rayDirection[1], 0.0f);
    CHECK_EQ(result->rayDirection[2], 1.0f);
}

GPU_TEST_CASE("ray-tracing-hitobject-make-hit", ALL | DontCreateDevice)
{
    // Limit the shader model to SM 6.6 for this test, since the NVAPI headers don't support MakeHit
    // for newer shader models.
    DeviceExtraOptions extraOptions;
    extraOptions.d3d12HighestShaderModel = 0x66; // SM 6.6
    device = createTestingDevice(ctx, ctx->deviceType, false, &extraOptions);
    REQUIRE(device);

    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    RayTracingSingleTriangleTest test;
    test.init(device);
    test.createResultBuffer(sizeof(TestResult));
    test.run(
        "ray-tracing/test-ray-tracing-hitobject-intrinsics-make-hit",
        "rayGenShaderMakeQueryInvokeHit",
        {"closestHitInvoke"},
        {"missNOP"}
    );

    ComPtr<ISlangBlob> resultBlob = test.getTestResult();
    checkQueryAndInvokeResult(resultBlob);
}
