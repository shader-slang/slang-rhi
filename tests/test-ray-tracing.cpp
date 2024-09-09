#include "testing.h"
#include "texture-utils.h"

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

    ComPtr<ITransientResourceHeap> transientHeap;
    ComPtr<ICommandQueue> queue;

    ComPtr<IPipeline> raytracingPipeline;
    ComPtr<IBuffer> vertexBuffer;
    ComPtr<IBuffer> indexBuffer;
    ComPtr<IBuffer> transformBuffer;
    ComPtr<IBuffer> instanceBuffer;
    ComPtr<IBuffer> BLASBuffer;
    ComPtr<IAccelerationStructure> BLAS;
    ComPtr<IBuffer> TLASBuffer;
    ComPtr<IAccelerationStructure> TLAS;
    ComPtr<ITexture> resultTexture;
    ComPtr<IResourceView> resultTextureUAV;
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
        resultTextureDesc.numMipLevels = 1;
        resultTextureDesc.size.width = width;
        resultTextureDesc.size.height = height;
        resultTextureDesc.size.depth = 1;
        resultTextureDesc.usage = TextureUsage::UnorderedAccess | TextureUsage::CopySource;
        resultTextureDesc.defaultState = ResourceState::UnorderedAccess;
        resultTextureDesc.format = Format::R32G32B32A32_FLOAT;
        resultTexture = device->createTexture(resultTextureDesc);
        IResourceView::Desc resultUAVDesc = {};
        resultUAVDesc.format = resultTextureDesc.format;
        resultUAVDesc.type = IResourceView::Type::UnorderedAccess;
        resultTextureUAV = device->createTextureView(resultTexture, resultUAVDesc);
    }

    void createRequiredResources()
    {
        ICommandQueue::Desc queueDesc = {};
        queueDesc.type = ICommandQueue::QueueType::Graphics;
        queue = device->createCommandQueue(queueDesc);

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

        ITransientResourceHeap::Desc transientHeapDesc = {};
        transientHeapDesc.constantBufferSize = 4096 * 1024;
        REQUIRE_CALL(device->createTransientResourceHeap(transientHeapDesc, transientHeap.writeRef()));

        // Build bottom level acceleration structure.
        {
            IAccelerationStructure::BuildInputs accelerationStructureBuildInputs;
            IAccelerationStructure::PrebuildInfo accelerationStructurePrebuildInfo;
            accelerationStructureBuildInputs.descCount = 1;
            accelerationStructureBuildInputs.kind = IAccelerationStructure::Kind::BottomLevel;
            accelerationStructureBuildInputs.flags = IAccelerationStructure::BuildFlags::AllowCompaction;
            IAccelerationStructure::GeometryDesc geomDesc;
            geomDesc.flags = IAccelerationStructure::GeometryFlags::Opaque;
            geomDesc.type = IAccelerationStructure::GeometryType::Triangles;
            geomDesc.content.triangles.indexCount = kIndexCount;
            geomDesc.content.triangles.indexData = indexBuffer->getDeviceAddress();
            geomDesc.content.triangles.indexFormat = Format::R32_UINT;
            geomDesc.content.triangles.vertexCount = kVertexCount;
            geomDesc.content.triangles.vertexData = vertexBuffer->getDeviceAddress();
            geomDesc.content.triangles.vertexFormat = Format::R32G32B32_FLOAT;
            geomDesc.content.triangles.vertexStride = sizeof(Vertex);
            geomDesc.content.triangles.transform3x4 = transformBuffer->getDeviceAddress();
            accelerationStructureBuildInputs.geometryDescs = &geomDesc;

            // Query buffer size for acceleration structure build.
            REQUIRE_CALL(device->getAccelerationStructurePrebuildInfo(
                accelerationStructureBuildInputs,
                &accelerationStructurePrebuildInfo
            ));
            // Allocate buffers for acceleration structure.
            BufferDesc asDraftBufferDesc;
            asDraftBufferDesc.usage = BufferUsage::AccelerationStructure;
            asDraftBufferDesc.defaultState = ResourceState::AccelerationStructure;
            asDraftBufferDesc.size = (size_t)accelerationStructurePrebuildInfo.resultDataMaxSize;
            ComPtr<IBuffer> draftBuffer = device->createBuffer(asDraftBufferDesc);
            BufferDesc scratchBufferDesc;
            scratchBufferDesc.usage = BufferUsage::UnorderedAccess;
            scratchBufferDesc.defaultState = ResourceState::UnorderedAccess;
            scratchBufferDesc.size = (size_t)accelerationStructurePrebuildInfo.scratchDataSize;
            ComPtr<IBuffer> scratchBuffer = device->createBuffer(scratchBufferDesc);

            // Build acceleration structure.
            ComPtr<IQueryPool> compactedSizeQuery;
            QueryPoolDesc queryPoolDesc;
            queryPoolDesc.count = 1;
            queryPoolDesc.type = QueryType::AccelerationStructureCompactedSize;
            REQUIRE_CALL(device->createQueryPool(queryPoolDesc, compactedSizeQuery.writeRef()));

            ComPtr<IAccelerationStructure> draftAS;
            IAccelerationStructure::CreateDesc draftCreateDesc;
            draftCreateDesc.buffer = draftBuffer;
            draftCreateDesc.kind = IAccelerationStructure::Kind::BottomLevel;
            draftCreateDesc.offset = 0;
            draftCreateDesc.size = accelerationStructurePrebuildInfo.resultDataMaxSize;
            REQUIRE_CALL(device->createAccelerationStructure(draftCreateDesc, draftAS.writeRef()));

            compactedSizeQuery->reset();

            auto commandBuffer = transientHeap->createCommandBuffer();
            auto encoder = commandBuffer->encodeRayTracingCommands();
            IAccelerationStructure::BuildDesc buildDesc = {};
            buildDesc.dest = draftAS;
            buildDesc.inputs = accelerationStructureBuildInputs;
            buildDesc.scratchData = scratchBuffer->getDeviceAddress();
            AccelerationStructureQueryDesc compactedSizeQueryDesc = {};
            compactedSizeQueryDesc.queryPool = compactedSizeQuery;
            compactedSizeQueryDesc.queryType = QueryType::AccelerationStructureCompactedSize;
            encoder->buildAccelerationStructure(buildDesc, 1, &compactedSizeQueryDesc);
            encoder->endEncoding();
            commandBuffer->close();
            queue->executeCommandBuffer(commandBuffer);
            queue->waitOnHost();

            uint64_t compactedSize = 0;
            compactedSizeQuery->getResult(0, 1, &compactedSize);
            BufferDesc asBufferDesc;
            asBufferDesc.usage = BufferUsage::AccelerationStructure;
            asBufferDesc.defaultState = ResourceState::AccelerationStructure;
            asBufferDesc.size = (size_t)compactedSize;
            BLASBuffer = device->createBuffer(asBufferDesc);
            IAccelerationStructure::CreateDesc createDesc;
            createDesc.buffer = BLASBuffer;
            createDesc.kind = IAccelerationStructure::Kind::BottomLevel;
            createDesc.offset = 0;
            createDesc.size = (size_t)compactedSize;
            device->createAccelerationStructure(createDesc, BLAS.writeRef());

            commandBuffer = transientHeap->createCommandBuffer();
            encoder = commandBuffer->encodeRayTracingCommands();
            encoder->copyAccelerationStructure(BLAS, draftAS, AccelerationStructureCopyMode::Compact);
            encoder->endEncoding();
            commandBuffer->close();
            queue->executeCommandBuffer(commandBuffer);
            queue->waitOnHost();
        }

        // Build top level acceleration structure.
        {
            std::vector<IAccelerationStructure::InstanceDesc> instanceDescs;
            instanceDescs.resize(1);
            instanceDescs[0].accelerationStructure = BLAS->getDeviceAddress();
            instanceDescs[0].flags = IAccelerationStructure::GeometryInstanceFlags::TriangleFacingCullDisable;
            instanceDescs[0].instanceContributionToHitGroupIndex = 0;
            instanceDescs[0].instanceID = 0;
            instanceDescs[0].instanceMask = 0xFF;
            float transformMatrix[] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
            memcpy(&instanceDescs[0].transform[0][0], transformMatrix, sizeof(float) * 12);

            BufferDesc instanceBufferDesc;
            instanceBufferDesc.size = instanceDescs.size() * sizeof(IAccelerationStructure::InstanceDesc);
            instanceBufferDesc.usage = BufferUsage::ShaderResource;
            instanceBufferDesc.defaultState = ResourceState::ShaderResource;
            instanceBuffer = device->createBuffer(instanceBufferDesc, instanceDescs.data());
            REQUIRE(instanceBuffer != nullptr);

            IAccelerationStructure::BuildInputs accelerationStructureBuildInputs = {};
            IAccelerationStructure::PrebuildInfo accelerationStructurePrebuildInfo = {};
            accelerationStructureBuildInputs.descCount = 1;
            accelerationStructureBuildInputs.kind = IAccelerationStructure::Kind::TopLevel;
            accelerationStructureBuildInputs.instanceDescs = instanceBuffer->getDeviceAddress();

            // Query buffer size for acceleration structure build.
            REQUIRE_CALL(device->getAccelerationStructurePrebuildInfo(
                accelerationStructureBuildInputs,
                &accelerationStructurePrebuildInfo
            ));

            BufferDesc asBufferDesc;
            asBufferDesc.usage = BufferUsage::AccelerationStructure;
            asBufferDesc.defaultState = ResourceState::AccelerationStructure;
            asBufferDesc.size = (size_t)accelerationStructurePrebuildInfo.resultDataMaxSize;
            TLASBuffer = device->createBuffer(asBufferDesc);

            BufferDesc scratchBufferDesc;
            scratchBufferDesc.usage = BufferUsage::UnorderedAccess;
            scratchBufferDesc.defaultState = ResourceState::UnorderedAccess;
            scratchBufferDesc.size = (size_t)accelerationStructurePrebuildInfo.scratchDataSize;
            ComPtr<IBuffer> scratchBuffer = device->createBuffer(scratchBufferDesc);

            IAccelerationStructure::CreateDesc createDesc;
            createDesc.buffer = TLASBuffer;
            createDesc.kind = IAccelerationStructure::Kind::TopLevel;
            createDesc.offset = 0;
            createDesc.size = (size_t)accelerationStructurePrebuildInfo.resultDataMaxSize;
            REQUIRE_CALL(device->createAccelerationStructure(createDesc, TLAS.writeRef()));

            auto commandBuffer = transientHeap->createCommandBuffer();
            auto encoder = commandBuffer->encodeRayTracingCommands();
            IAccelerationStructure::BuildDesc buildDesc = {};
            buildDesc.dest = TLAS;
            buildDesc.inputs = accelerationStructureBuildInputs;
            buildDesc.scratchData = scratchBuffer->getDeviceAddress();
            encoder->buildAccelerationStructure(buildDesc, 0, nullptr);
            encoder->endEncoding();
            commandBuffer->close();
            queue->executeCommandBuffer(commandBuffer);
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

        IShaderTable::Desc shaderTableDesc = {};
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
        auto cmdBuffer = transientHeap->createCommandBuffer();
        auto encoder = cmdBuffer->encodeResourceCommands();
        encoder->textureBarrier(resultTexture.get(), ResourceState::UnorderedAccess, ResourceState::CopySource);
        encoder->endEncoding();
        cmdBuffer->close();
        queue->executeCommandBuffer(cmdBuffer.get());
        queue->waitOnHost();

        REQUIRE_CALL(
            device->readTexture(resultTexture, ResourceState::CopySource, resultBlob.writeRef(), &rowPitch, &pixelSize)
        );
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
        ComPtr<ICommandBuffer> renderCommandBuffer = transientHeap->createCommandBuffer();
        auto renderEncoder = renderCommandBuffer->encodeRayTracingCommands();
        IShaderObject* rootObject = nullptr;
        renderEncoder->bindPipeline(raytracingPipeline, &rootObject);
        auto cursor = ShaderCursor(rootObject);
        cursor["resultTexture"].setResource(resultTextureUAV);
        cursor["sceneBVH"].setResource(TLAS);
        renderEncoder->dispatchRays(0, shaderTable, width, height, 1);
        renderEncoder->endEncoding();
        renderCommandBuffer->close();
        queue->executeCommandBuffer(renderCommandBuffer);
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
        ComPtr<ICommandBuffer> renderCommandBuffer = transientHeap->createCommandBuffer();
        auto renderEncoder = renderCommandBuffer->encodeRayTracingCommands();
        IShaderObject* rootObject = nullptr;
        renderEncoder->bindPipeline(raytracingPipeline, &rootObject);
        auto cursor = ShaderCursor(rootObject);
        cursor["resultTexture"].setResource(resultTextureUAV);
        cursor["sceneBVH"].setResource(TLAS);
        renderEncoder->dispatchRays(1, shaderTable, width, height, 1);
        renderEncoder->endEncoding();
        renderCommandBuffer->close();
        queue->executeCommandBuffer(renderCommandBuffer);
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

template<typename T>
void testRayTracing(GpuTestContext* ctx, DeviceType deviceType)
{
    ComPtr<IDevice> device = createTestingDevice(ctx, deviceType);
    if (!device->hasFeature("ray-tracing"))
        SKIP("ray tracing not supported");
    T test;
    test.init(device);
    test.run();
}

TEST_CASE("ray-tracing-a")
{
    runGpuTests(
        testRayTracing<RayTracingTestA>,
        {
            DeviceType::D3D12,
            DeviceType::Vulkan,
        }
    );
}

TEST_CASE("ray-tracing-b")
{
    runGpuTests(
        testRayTracing<RayTracingTestB>,
        {
            DeviceType::D3D12,
            DeviceType::Vulkan,
        }
    );
}
