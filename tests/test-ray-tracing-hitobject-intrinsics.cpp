#include "testing.h"
#include "texture-utils.h"

#include <slang-rhi/acceleration-structure-utils.h>

using namespace rhi;
using namespace rhi::testing;

// Vertex and index data for a single triangle. We don't actually use it, but slang-rhi disallows non-empty accel
// structures.

struct Vertex
{
    float position[3];
};

static const int kVertexCount = 3;
static const Vertex kVertexData[kVertexCount] = {
    {0.f, 0.f, 1.f},
    {1.f, 0.f, 1.f},
    {0.f, 1.f, 1.f},
};

static const int kIndexCount = 3;
static const uint32_t kIndexData[kIndexCount] = {0, 1, 2};

struct TestResult
{
    int queryWasSuccess;
    int invokeWasSuccess;

    float rayOrigin[3];
    float rayDirection[3];
};

struct TestResultCudaAligned
{
    int queryWasSuccess;
    int invokeWasSuccess;

    float rayOrigin[3];
    float rayDirection[3];
};

struct RayTracingHitObjectIntrinsicsTest
{
    IDevice* device;

    ComPtr<ICommandQueue> queue;

    ComPtr<IRayTracingPipeline> raytracingPipeline;
    ComPtr<IBuffer> vertexBuffer;
    ComPtr<IBuffer> indexBuffer;
    ComPtr<IBuffer> instanceBuffer;
    ComPtr<IBuffer> BLASBuffer;
    ComPtr<IAccelerationStructure> BLAS;
    ComPtr<IBuffer> TLASBuffer;
    ComPtr<IAccelerationStructure> TLAS;
    ComPtr<IShaderTable> shaderTable;

    void init(IDevice* device_) { this->device = device_; }

    Result loadShaderPrograms(
        const char* moduleName,
        const char* raygenName,
        const char* closestHitName,
        const char* missName,
        const char* closestHitName2 = nullptr,
        const char* missName2 = nullptr,
        IShaderProgram** outProgram = nullptr
    )
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

        SLANG_RETURN_ON_FAIL(module->findEntryPointByName(raygenName, entryPoint.writeRef()));
        componentTypes.push_back(entryPoint);

        SLANG_RETURN_ON_FAIL(module->findEntryPointByName(missName, entryPoint.writeRef()));
        componentTypes.push_back(entryPoint);

        if (missName2)
        {
            SLANG_RETURN_ON_FAIL(module->findEntryPointByName(missName2, entryPoint.writeRef()));
            componentTypes.push_back(entryPoint);
        }

        SLANG_RETURN_ON_FAIL(module->findEntryPointByName(closestHitName, entryPoint.writeRef()));
        componentTypes.push_back(entryPoint);

        if (closestHitName2)
        {
            SLANG_RETURN_ON_FAIL(module->findEntryPointByName(closestHitName2, entryPoint.writeRef()));
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

    ComPtr<IBuffer> resultBuffer;

    void createResultBuffer()
    {
        const size_t resultSize =
            device->getDeviceType() == DeviceType::CUDA ? sizeof(TestResultCudaAligned) : sizeof(TestResult);

        BufferDesc resultBufferDesc = {};
        resultBufferDesc.size = resultSize;
        resultBufferDesc.elementSize = resultSize;
        resultBufferDesc.memoryType = MemoryType::DeviceLocal;
        resultBufferDesc.usage = BufferUsage::UnorderedAccess | BufferUsage::CopySource;
        resultBuffer = device->createBuffer(resultBufferDesc);
        REQUIRE(resultBuffer != nullptr);
    }

    void createRequiredResources(
        const char* moduleName,
        const char* raygenName,
        const char* closestHitName,
        const char* missName,
        const char* closestHitName2 = nullptr,
        const char* missName2 = nullptr
    )
    {
        queue = device->getQueue(QueueType::Graphics);

        createResultBuffer();

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

        // Build bottom level acceleration structure.
        {
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

        // Build top level acceleration structure.
        {
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

        ComPtr<IShaderProgram> rayTracingProgram;
        REQUIRE_CALL(loadShaderPrograms(
            moduleName,
            raygenName,
            closestHitName,
            missName,
            closestHitName2,
            missName2,
            rayTracingProgram.writeRef()
        ));

        const char* hitgroupNames[] = {"hitgroup1", "hitgroup2"};

        HitGroupDesc hitGroups[2];

        hitGroups[0].hitGroupName = hitgroupNames[0];
        hitGroups[0].closestHitEntryPoint = closestHitName;

        if (closestHitName2)
        {
            hitGroups[1].hitGroupName = hitgroupNames[1];
            hitGroups[1].closestHitEntryPoint = closestHitName2;
        }

        unsigned hitGroupCount = closestHitName2 ? 2 : 1;
        unsigned missShaderCount = missName2 ? 2 : 1;

        RayTracingPipelineDesc rtpDesc = {};
        rtpDesc.program = rayTracingProgram;
        rtpDesc.hitGroupCount = hitGroupCount;
        rtpDesc.hitGroups = hitGroups;
        rtpDesc.maxRayPayloadSize = 64;
        rtpDesc.maxAttributeSizeInBytes = 8;
        rtpDesc.maxRecursion = 2;
        REQUIRE_CALL(device->createRayTracingPipeline(rtpDesc, raytracingPipeline.writeRef()));
        REQUIRE(raytracingPipeline != nullptr);

        const char* missNames[] = {missName, missName2};

        ShaderTableDesc shaderTableDesc = {};
        shaderTableDesc.program = rayTracingProgram;
        shaderTableDesc.hitGroupCount = hitGroupCount;
        shaderTableDesc.hitGroupNames = hitgroupNames;
        shaderTableDesc.rayGenShaderCount = 1;
        shaderTableDesc.rayGenShaderEntryPointNames = &raygenName;
        shaderTableDesc.missShaderCount = missShaderCount;
        shaderTableDesc.missShaderEntryPointNames = missNames;
        REQUIRE_CALL(device->createShaderTable(shaderTableDesc, shaderTable.writeRef()));
    }

    template<typename T>
    void checkQueryAndInvokeResults()
    {
        ComPtr<ISlangBlob> resultBlob;
        REQUIRE_CALL(device->readBuffer(resultBuffer, 0, sizeof(T), resultBlob.writeRef()));

        const T* result = reinterpret_cast<const T*>(resultBlob->getBufferPointer());

        CHECK_EQ(result->queryWasSuccess, 1);
        CHECK_EQ(result->invokeWasSuccess, 1);
    }

    void checkQueryAndInvokeResults()
    {
        if (device->getDeviceType() == DeviceType::CUDA)
            checkQueryAndInvokeResults<TestResultCudaAligned>();
        else
            checkQueryAndInvokeResults<TestResult>();
    }

    void renderFrame()
    {
        auto commandEncoder = queue->createCommandEncoder();

        auto passEncoder = commandEncoder->beginRayTracingPass();
        auto rootObject = passEncoder->bindPipeline(raytracingPipeline, shaderTable);
        auto cursor = ShaderCursor(rootObject);
        cursor["resultBuffer"].setBinding(resultBuffer);
        cursor["sceneBVH"].setBinding(TLAS);
        passEncoder->dispatchRays(0, 1, 1, 1);
        passEncoder->end();

        queue->submit(commandEncoder->finish());
        queue->waitOnHost();
    }

    void run(
        const char* moduleName,
        const char* raygenName,
        const char* closestHitName,
        const char* missName,
        const char* closestHitName2 = nullptr,
        const char* missName2 = nullptr
    )
    {
        createRequiredResources(moduleName, raygenName, closestHitName, missName, closestHitName2, missName2);
        renderFrame();
    }
};

GPU_TEST_CASE("ray-tracing-hitobject-query-invoke-nop-rg", ALL)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    RayTracingHitObjectIntrinsicsTest test;
    test.init(device);
    test.run("test-ray-tracing-hitobject-intrinsics", "rayGenShaderMakeQueryInvokeNOP", "closestHitNOP", "missNOP");
    test.checkQueryAndInvokeResults();
}

GPU_TEST_CASE("ray-tracing-hitobject-query-invoke-nop-ch", ALL)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    RayTracingHitObjectIntrinsicsTest test;
    test.init(device);
    test.run(
        "test-ray-tracing-hitobject-intrinsics",
        "rayGenShaderInvokeCH",
        "closestHitMakeQueryInvokeNOP",
        "missNOP"
    );
    test.checkQueryAndInvokeResults();
}

GPU_TEST_CASE("ray-tracing-hitobject-query-invoke-nop-ms", ALL)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    RayTracingHitObjectIntrinsicsTest test;
    test.init(device);
    test.run(
        "test-ray-tracing-hitobject-intrinsics",
        "rayGenShaderInvokeMS",
        "closestHitNOP",
        "missMakeQueryInvokeNOP"
    );
    test.checkQueryAndInvokeResults();
}

GPU_TEST_CASE("ray-tracing-hitobject-query-invoke-miss-rg", ALL)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    RayTracingHitObjectIntrinsicsTest test;
    test.init(device);
    test.run("test-ray-tracing-hitobject-intrinsics", "rayGenShaderMakeQueryInvokeMiss", "closestHitNOP", "missInvoke");
    test.checkQueryAndInvokeResults();
}

GPU_TEST_CASE("ray-tracing-hitobject-query-invoke-miss-ch", ALL)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    RayTracingHitObjectIntrinsicsTest test;
    test.init(device);
    test.run(
        "test-ray-tracing-hitobject-intrinsics",
        "rayGenShaderInvokeCH",
        "closestHitMakeQueryInvokeMiss",
        "missInvoke"
    );
    test.checkQueryAndInvokeResults();
}

GPU_TEST_CASE("ray-tracing-hitobject-query-invoke-miss-ms", ALL)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    RayTracingHitObjectIntrinsicsTest test;
    test.init(device);
    test.run(
        "test-ray-tracing-hitobject-intrinsics",
        "rayGenShaderInvokeMS",
        "closestHitNOP",
        "missMakeQueryInvokeMiss",
        nullptr,
        "missInvoke"
    );
    test.checkQueryAndInvokeResults();
}

GPU_TEST_CASE("ray-tracing-hitobject-query-invoke-hit-rg", ALL)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    RayTracingHitObjectIntrinsicsTest test;
    test.init(device);
    test.run("test-ray-tracing-hitobject-intrinsics", "rayGenShaderTraceQueryInvokeHit", "closestHitInvoke", "missNOP");
    test.checkQueryAndInvokeResults();
}

GPU_TEST_CASE("ray-tracing-hitobject-query-invoke-hit-ch", ALL)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    RayTracingHitObjectIntrinsicsTest test;
    test.init(device);
    test.run(
        "test-ray-tracing-hitobject-intrinsics",
        "rayGenShaderInvokeCH",
        "closestHitMakeQueryInvokeHit",
        "missNOP",
        "closestHitInvoke",
        nullptr
    );
    test.checkQueryAndInvokeResults();
}

GPU_TEST_CASE("ray-tracing-hitobject-query-invoke-hit-ms", ALL)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    RayTracingHitObjectIntrinsicsTest test;
    test.init(device);
    test.run(
        "test-ray-tracing-hitobject-intrinsics",
        "rayGenShaderInvokeMS",
        "closestHitNOP",
        "missMakeQueryInvokeHit",
        "closestHitInvoke",
        nullptr
    );
    test.checkQueryAndInvokeResults();
}

// CUDA/OptiX is disabled because it only supports getting the ray origin in world space.
// D3D12 is disabled due to https://github.com/shader-slang/slang/issues/8615
GPU_TEST_CASE("ray-tracing-hitobject-query-hit-ray-object-origin", ALL & ~CUDA & ~D3D12)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    RayTracingHitObjectIntrinsicsTest test;
    test.init(device);
    test.run("test-ray-tracing-hitobject-intrinsics", "rayGenShaderQueryRayObjectOrigin", "closestHitNOP", "missNOP");

    ComPtr<ISlangBlob> resultBlob;

    if (device->getDeviceType() == DeviceType::CUDA)
    {
        REQUIRE_CALL(device->readBuffer(test.resultBuffer, 0, sizeof(TestResultCudaAligned), resultBlob.writeRef()));
        const TestResultCudaAligned* result =
            reinterpret_cast<const TestResultCudaAligned*>(resultBlob->getBufferPointer());
        CHECK_EQ(result->rayOrigin[0], 0.1f);
        CHECK_EQ(result->rayOrigin[1], 0.1f);
        CHECK_EQ(result->rayOrigin[2], 0.1f);
    }
    else
    {
        REQUIRE_CALL(device->readBuffer(test.resultBuffer, 0, sizeof(TestResult), resultBlob.writeRef()));
        const TestResult* result = reinterpret_cast<const TestResult*>(resultBlob->getBufferPointer());
        CHECK_EQ(result->rayOrigin[0], 0.1f);
        CHECK_EQ(result->rayOrigin[1], 0.1f);
        CHECK_EQ(result->rayOrigin[2], 0.1f);
    }
}

// Disabled under CUDA/OptiX and D3D12 due to https://github.com/shader-slang/slang/issues/8615
GPU_TEST_CASE("ray-tracing-hitobject-query-hit-ray-object-direction", ALL & ~CUDA & ~D3D12)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    RayTracingHitObjectIntrinsicsTest test;
    test.init(device);
    test.run(
        "test-ray-tracing-hitobject-intrinsics",
        "rayGenShaderQueryRayObjectDirection",
        "closestHitNOP",
        "missNOP"
    );

    ComPtr<ISlangBlob> resultBlob;

    if (device->getDeviceType() == DeviceType::CUDA)
    {
        REQUIRE_CALL(device->readBuffer(test.resultBuffer, 0, sizeof(TestResultCudaAligned), resultBlob.writeRef()));
        const TestResultCudaAligned* result =
            reinterpret_cast<const TestResultCudaAligned*>(resultBlob->getBufferPointer());
        CHECK_EQ(result->rayDirection[0], 0.0f);
        CHECK_EQ(result->rayDirection[1], 0.0f);
        CHECK_EQ(result->rayDirection[2], 1.0f);
    }
    else
    {
        REQUIRE_CALL(device->readBuffer(test.resultBuffer, 0, sizeof(TestResult), resultBlob.writeRef()));
        const TestResult* result = reinterpret_cast<const TestResult*>(resultBlob->getBufferPointer());
        CHECK_EQ(result->rayDirection[0], 0.0f);
        CHECK_EQ(result->rayDirection[1], 0.0f);
        CHECK_EQ(result->rayDirection[2], 1.0f);
    }
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

    RayTracingHitObjectIntrinsicsTest test;
    test.init(device);
    test.run(
        "test-ray-tracing-hitobject-intrinsics-make-hit",
        "rayGenShaderMakeQueryInvokeHit",
        "closestHitInvoke",
        "missNOP"
    );
    test.checkQueryAndInvokeResults();
}
