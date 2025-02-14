#include "testing.h"
#include "texture-utils.h"

#include <slang-rhi/acceleration-structure-utils.h>

using namespace rhi;
using namespace rhi::testing;

struct Vertex
{
    float position[3];
};

static const int kVertexCount = 9;
static const Vertex kVertexData[kVertexCount] = {
    // Triangle 1
    {0, 0, 1},
    {4, 0, 1},
    {0, 4, 1},

    // Triangle 2
    {-4, 0, 1},
    {0, 0, 1},
    {0, 4, 1},

    // Triangle 3
    {0, 0, 1},
    {4, 0, 1},
    {0, -4, 1},
};
static const int kIndexCount = 9;
static const uint32_t kIndexData[kIndexCount] = {
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

struct BaseRayTracingTest
{
    IDevice* device;

    ComPtr<ICommandQueue> queue;

    ComPtr<IRayTracingPipeline> raytracingPipeline;
    ComPtr<IBuffer> vertexBuffer;
    ComPtr<IBuffer> indexBuffer;
    ComPtr<IBuffer> transformBuffer;
    ComPtr<IBuffer> instanceBuffer;
    ComPtr<IBuffer> BLASBuffer;
    ComPtr<IAccelerationStructure> BLAS;
    ComPtr<IBuffer> TLASBuffer;
    ComPtr<IAccelerationStructure> TLAS;
    ComPtr<ITexture> resultTexture;
    ComPtr<IShaderTable> shaderTable;

    uint32_t width = 2;
    uint32_t height = 2;

    void init(IDevice* device) { this->device = device; }

    // Load and compile shader code from source.
    Result loadShaderProgram(IDevice* device, IShaderProgram** outProgram)
    {
        ComPtr<slang::ISession> slangSession;
        slangSession = device->getSlangSession();

        ComPtr<slang::IBlob> diagnosticsBlob;
        slang::IModule* module = slangSession->loadModule("test-ray-tracing", diagnosticsBlob.writeRef());
        if (!module)
            return SLANG_FAIL;

        std::vector<slang::IComponentType*> componentTypes;
        componentTypes.push_back(module);
        ComPtr<slang::IEntryPoint> entryPoint;
        SLANG_RETURN_ON_FAIL(module->findEntryPointByName("rayGenShaderA", entryPoint.writeRef()));
        componentTypes.push_back(entryPoint);
        SLANG_RETURN_ON_FAIL(module->findEntryPointByName("rayGenShaderB", entryPoint.writeRef()));
        componentTypes.push_back(entryPoint);
        SLANG_RETURN_ON_FAIL(module->findEntryPointByName("missShaderA", entryPoint.writeRef()));
        componentTypes.push_back(entryPoint);
        SLANG_RETURN_ON_FAIL(module->findEntryPointByName("missShaderB", entryPoint.writeRef()));
        componentTypes.push_back(entryPoint);
        SLANG_RETURN_ON_FAIL(module->findEntryPointByName("closestHitShaderA", entryPoint.writeRef()));
        componentTypes.push_back(entryPoint);
        SLANG_RETURN_ON_FAIL(module->findEntryPointByName("closestHitShaderB", entryPoint.writeRef()));
        componentTypes.push_back(entryPoint);

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

    void createResultTexture()
    {
        TextureDesc resultTextureDesc = {};
        resultTextureDesc.type = TextureType::Texture2D;
        resultTextureDesc.mipLevelCount = 1;
        resultTextureDesc.size.width = width;
        resultTextureDesc.size.height = height;
        resultTextureDesc.size.depth = 1;
        resultTextureDesc.usage = TextureUsage::UnorderedAccess | TextureUsage::CopySource;
        resultTextureDesc.defaultState = ResourceState::UnorderedAccess;
        resultTextureDesc.format = Format::R32G32B32A32_FLOAT;
        resultTexture = device->createTexture(resultTextureDesc);
    }

    void createRequiredResources()
    {
        queue = device->getQueue(QueueType::Graphics);

        BufferDesc vertexBufferDesc;
        vertexBufferDesc.size = kVertexCount * sizeof(Vertex);
        vertexBufferDesc.usage = BufferUsage::ShaderResource;
        vertexBufferDesc.defaultState = ResourceState::ShaderResource;
        vertexBuffer = device->createBuffer(vertexBufferDesc, &kVertexData[0]);
        REQUIRE(vertexBuffer != nullptr);

        BufferDesc indexBufferDesc;
        indexBufferDesc.size = kIndexCount * sizeof(int32_t);
        indexBufferDesc.usage = BufferUsage::ShaderResource;
        indexBufferDesc.defaultState = ResourceState::ShaderResource;
        indexBuffer = device->createBuffer(indexBufferDesc, &kIndexData[0]);
        REQUIRE(indexBuffer != nullptr);

        BufferDesc transformBufferDesc;
        transformBufferDesc.size = sizeof(float) * 12;
        transformBufferDesc.usage = BufferUsage::ShaderResource;
        transformBufferDesc.defaultState = ResourceState::ShaderResource;
        float transformData[12] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
        transformBuffer = device->createBuffer(transformBufferDesc, &transformData);
        REQUIRE(transformBuffer != nullptr);

        createResultTexture();

        // Build bottom level acceleration structure.
        {
            AccelerationStructureBuildInputTriangles triangles = {};
            BufferWithOffset vertexBufferWithOffset = vertexBuffer;
            triangles.vertexBuffers = &vertexBufferWithOffset;
            triangles.vertexBufferCount = 1;
            triangles.vertexFormat = Format::R32G32B32_FLOAT;
            triangles.vertexCount = kVertexCount;
            triangles.vertexStride = sizeof(Vertex);
            triangles.indexBuffer = indexBuffer;
            triangles.indexFormat = IndexFormat::UInt32;
            triangles.indexCount = kIndexCount;
            triangles.preTransformBuffer = transformBuffer;
            triangles.flags = AccelerationStructureGeometryFlags::Opaque;
            AccelerationStructureBuildDesc buildDesc = {};
            buildDesc.inputs = &triangles;
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
            genericInstanceDescs[0].flags = AccelerationStructureInstanceFlags::TriangleFacingCullDisable;
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

            AccelerationStructureBuildInputInstances instances = {};
            instances.instanceBuffer = instanceBuffer;
            instances.instanceCount = 1;
            instances.instanceStride = nativeInstanceDescSize;
            AccelerationStructureBuildDesc buildDesc = {};
            buildDesc.inputs = &instances;
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

        const char* hitgroupNames[] = {"hitgroupA", "hitgroupB"};

        ComPtr<IShaderProgram> rayTracingProgram;
        REQUIRE_CALL(loadShaderProgram(device, rayTracingProgram.writeRef()));
        RayTracingPipelineDesc rtpDesc = {};
        rtpDesc.program = rayTracingProgram;
        rtpDesc.hitGroupCount = 2;
        HitGroupDesc hitGroups[2];
        hitGroups[0].closestHitEntryPoint = "closestHitShaderA";
        hitGroups[0].hitGroupName = hitgroupNames[0];
        hitGroups[1].closestHitEntryPoint = "closestHitShaderB";
        hitGroups[1].hitGroupName = hitgroupNames[1];
        rtpDesc.hitGroups = hitGroups;
        rtpDesc.maxRayPayloadSize = 64;
        rtpDesc.maxRecursion = 2;
        REQUIRE_CALL(device->createRayTracingPipeline(rtpDesc, raytracingPipeline.writeRef()));
        REQUIRE(raytracingPipeline != nullptr);

        const char* raygenNames[] = {"rayGenShaderA", "rayGenShaderB"};
        const char* missNames[] = {"missShaderA", "missShaderB"};

        ShaderTableDesc shaderTableDesc = {};
        shaderTableDesc.program = rayTracingProgram;
        shaderTableDesc.hitGroupCount = 2;
        shaderTableDesc.hitGroupNames = hitgroupNames;
        shaderTableDesc.rayGenShaderCount = 2;
        shaderTableDesc.rayGenShaderEntryPointNames = raygenNames;
        shaderTableDesc.missShaderCount = 2;
        shaderTableDesc.missShaderEntryPointNames = missNames;
        REQUIRE_CALL(device->createShaderTable(shaderTableDesc, shaderTable.writeRef()));
    }

    void checkTestResults(float* expectedResult, uint32_t count)
    {
        ComPtr<ISlangBlob> resultBlob;
        size_t rowPitch = 0;
        size_t pixelSize = 0;
        REQUIRE_CALL(device->readTexture(resultTexture, resultBlob.writeRef(), &rowPitch, &pixelSize));
#if 0 // for debugging only
        writeImage("test.hdr", resultBlob, width, height, (uint32_t)rowPitch, (uint32_t)pixelSize);
#endif
        auto buffer = removePadding(resultBlob, width, height, rowPitch, pixelSize);
        auto actualData = (float*)buffer.data();
        CHECK(memcmp(actualData, expectedResult, count * sizeof(float)) == 0);
    }
};

struct RayTracingTestA : BaseRayTracingTest
{
    void renderFrame()
    {
        auto commandEncoder = queue->createCommandEncoder();

        auto passEncoder = commandEncoder->beginRayTracingPass();
        auto rootObject = passEncoder->bindPipeline(raytracingPipeline, shaderTable);
        auto cursor = ShaderCursor(rootObject);
        cursor["resultTexture"].setBinding(resultTexture);
        cursor["sceneBVH"].setBinding(TLAS);
        passEncoder->dispatchRays(0, width, height, 1);
        passEncoder->end();

        queue->submit(commandEncoder->finish());
        queue->waitOnHost();
    }

    void run()
    {
        createRequiredResources();
        renderFrame();

        float expectedResult[16] = {1, 1, 1, 1, 0, 0, 1, 1, 0, 1, 0, 1, 1, 0, 0, 1};
        checkTestResults(expectedResult, 16);
    }
};

struct RayTracingTestB : BaseRayTracingTest
{
    void renderFrame()
    {
        auto commandEncoder = queue->createCommandEncoder();

        auto passEncoder = commandEncoder->beginRayTracingPass();
        auto rootObject = passEncoder->bindPipeline(raytracingPipeline, shaderTable);
        auto cursor = ShaderCursor(rootObject);
        cursor["resultTexture"].setBinding(resultTexture);
        cursor["sceneBVH"].setBinding(TLAS);
        passEncoder->dispatchRays(1, width, height, 1);
        passEncoder->end();

        queue->submit(commandEncoder->finish());
        queue->waitOnHost();
    }

    void run()
    {
        createRequiredResources();
        renderFrame();

        float expectedResult[16] = {0, 0, 0, 1, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 1};
        checkTestResults(expectedResult, 16);
    }
};

GPU_TEST_CASE("ray-tracing-a", ALL)
{
    if (!device->hasFeature("ray-tracing"))
        SKIP("ray tracing not supported");

    RayTracingTestA test;
    test.init(device);
    test.run();
}

GPU_TEST_CASE("ray-tracing-b", ALL)
{
    if (!device->hasFeature("ray-tracing"))
        SKIP("ray tracing not supported");

    RayTracingTestB test;
    test.init(device);
    test.run();
}
