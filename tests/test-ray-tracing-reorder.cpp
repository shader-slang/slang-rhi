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

struct ExpectedPixel
{
    uint32_t pos[2];
    float color[4];
};

#define EXPECTED_PIXEL(x, y, r, g, b, a)                                                                               \
    {                                                                                                                  \
        {x, y},                                                                                                        \
        {                                                                                                              \
            r, g, b, a                                                                                                 \
        }                                                                                                              \
    }

const char* kClosestHitEntryPoint = "closestHitShader";
const char* kMissEntryPoint = "missShader";

struct RayTracingReorderTest
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
    ComPtr<IShaderTable> shaderTable;

    uint32_t width = 128;
    uint32_t height = 128;

    ComPtr<ITexture> resultTexture;


    void init(IDevice* device_) { this->device = device_; }

    // Load and compile shader code from source.
    Result loadShaderProgram(const char* raygenName, IShaderProgram** outProgram)
    {
        ComPtr<slang::ISession> slangSession;
        slangSession = device->getSlangSession();

        ComPtr<slang::IBlob> diagnosticsBlob;
        slang::IModule* module = slangSession->loadModule("test-ray-tracing-reorder", diagnosticsBlob.writeRef());
        diagnoseIfNeeded(diagnosticsBlob);
        if (!module)
            return SLANG_FAIL;

        std::vector<slang::IComponentType*> componentTypes;
        componentTypes.push_back(module);

        ComPtr<slang::IEntryPoint> entryPoint;

        SLANG_RETURN_ON_FAIL(module->findEntryPointByName(raygenName, entryPoint.writeRef()));
        componentTypes.push_back(entryPoint);

        SLANG_RETURN_ON_FAIL(module->findEntryPointByName(kMissEntryPoint, entryPoint.writeRef()));
        componentTypes.push_back(entryPoint);

        SLANG_RETURN_ON_FAIL(module->findEntryPointByName(kClosestHitEntryPoint, entryPoint.writeRef()));
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
        resultTextureDesc.mipCount = 1;
        resultTextureDesc.size.width = width;
        resultTextureDesc.size.height = height;
        resultTextureDesc.size.depth = 1;
        resultTextureDesc.usage = TextureUsage::UnorderedAccess | TextureUsage::CopySource;
        resultTextureDesc.defaultState = ResourceState::UnorderedAccess;
        resultTextureDesc.format = Format::RGBA32Float;
        resultTexture = device->createTexture(resultTextureDesc);
    }

    void createRequiredResources(const char* raygenName)
    {
        queue = device->getQueue(QueueType::Graphics);

        BufferDesc vertexBufferDesc;
        vertexBufferDesc.size = kVertexCount * sizeof(Vertex);
        vertexBufferDesc.usage = BufferUsage::AccelerationStructureBuildInput;
        vertexBufferDesc.defaultState = ResourceState::AccelerationStructureBuildInput;
        vertexBuffer = device->createBuffer(vertexBufferDesc, kVertexData);
        REQUIRE(vertexBuffer != nullptr);

        BufferDesc indexBufferDesc;
        indexBufferDesc.size = kIndexCount * sizeof(uint32_t);
        indexBufferDesc.usage = BufferUsage::AccelerationStructureBuildInput;
        indexBufferDesc.defaultState = ResourceState::AccelerationStructureBuildInput;
        indexBuffer = device->createBuffer(indexBufferDesc, kIndexData);
        REQUIRE(indexBuffer != nullptr);

        createResultTexture();

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
            buildInput.triangles.preTransformBuffer = transformBuffer;
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

        const char* hitgroupNames[] = {"hitgroup"};

        ComPtr<IShaderProgram> rayTracingProgram;
        REQUIRE_CALL(loadShaderProgram(raygenName, rayTracingProgram.writeRef()));

        HitGroupDesc hitGroups[1];
        hitGroups[0].hitGroupName = hitgroupNames[0];
        hitGroups[0].closestHitEntryPoint = kClosestHitEntryPoint;

        RayTracingPipelineDesc rtpDesc = {};
        rtpDesc.program = rayTracingProgram;
        rtpDesc.hitGroupCount = 1;
        rtpDesc.hitGroups = hitGroups;
        rtpDesc.maxRayPayloadSize = 64;
        rtpDesc.maxAttributeSizeInBytes = 8;
        rtpDesc.maxRecursion = 2;
        REQUIRE_CALL(device->createRayTracingPipeline(rtpDesc, raytracingPipeline.writeRef()));
        REQUIRE(raytracingPipeline != nullptr);

        ShaderTableDesc shaderTableDesc = {};
        shaderTableDesc.program = rayTracingProgram;
        shaderTableDesc.hitGroupCount = 1;
        shaderTableDesc.hitGroupNames = hitgroupNames;
        shaderTableDesc.rayGenShaderCount = 1;
        shaderTableDesc.rayGenShaderEntryPointNames = &raygenName;
        shaderTableDesc.missShaderCount = 1;
        shaderTableDesc.missShaderEntryPointNames = &kMissEntryPoint;
        REQUIRE_CALL(device->createShaderTable(shaderTableDesc, shaderTable.writeRef()));
    }

    void checkTestResults()
    {
        ComPtr<ISlangBlob> resultBlob;
        SubresourceLayout layout;
        REQUIRE_CALL(device->readTexture(resultTexture, 0, 0, resultBlob.writeRef(), &layout));
#if 0 // for debugging only
        writeImage("test-ray-tracing-reorder.hdr", resultBlob, width, height, layout.rowPitch, layout.colPitch);
#endif

        ExpectedPixel expectedPixels[] = {
            EXPECTED_PIXEL(64, 64, 1.f, 0.f, 0.f, 1.f), // Triangle 1
            EXPECTED_PIXEL(63, 64, 0.f, 1.f, 0.f, 1.f), // Triangle 2
            EXPECTED_PIXEL(64, 63, 0.f, 0.f, 1.f, 1.f), // Triangle 3
            EXPECTED_PIXEL(63, 63, 1.f, 1.f, 1.f, 1.f), // Miss
            // Corners should all be misses
            EXPECTED_PIXEL(0, 0, 1.f, 1.f, 1.f, 1.f),     // Miss
            EXPECTED_PIXEL(127, 0, 1.f, 1.f, 1.f, 1.f),   // Miss
            EXPECTED_PIXEL(127, 127, 1.f, 1.f, 1.f, 1.f), // Miss
            EXPECTED_PIXEL(0, 127, 1.f, 1.f, 1.f, 1.f),   // Miss
        };

        for (const auto& ep : expectedPixels)
        {
            uint32_t x = ep.pos[0];
            uint32_t y = ep.pos[1];
            const float* color = reinterpret_cast<const float*>(
                static_cast<const uint8_t*>(resultBlob->getBufferPointer()) + y * layout.rowPitch + x * layout.colPitch
            );
            CAPTURE(x);
            CAPTURE(y);
            CHECK_EQ(color[0], ep.color[0]);
            CHECK_EQ(color[1], ep.color[1]);
            CHECK_EQ(color[2], ep.color[2]);
            CHECK_EQ(color[3], ep.color[3]);
        }
    }

    void renderFrame()
    {
        auto commandEncoder = queue->createCommandEncoder();

        auto passEncoder = commandEncoder->beginRayTracingPass();
        auto rootObject = passEncoder->bindPipeline(raytracingPipeline, shaderTable);
        auto cursor = ShaderCursor(rootObject);
        uint32_t dims[2] = {width, height};
        cursor["dims"].setData(dims, sizeof(dims));
        cursor["resultTexture"].setBinding(resultTexture);
        cursor["sceneBVH"].setBinding(TLAS);
        passEncoder->dispatchRays(0, width, height, 1);
        passEncoder->end();

        queue->submit(commandEncoder->finish());
        queue->waitOnHost();
    }

    void run(const char* raygenName)
    {
        createRequiredResources(raygenName);
        createResultTexture();
        renderFrame();
        checkTestResults();
    }
};

GPU_TEST_CASE("ray-tracing-reorder-hint", ALL)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    RayTracingReorderTest test;
    test.init(device);
    test.run("rayGenShaderReorderHint");
}

GPU_TEST_CASE("ray-tracing-reorder-hit-obj", ALL)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    RayTracingReorderTest test;
    test.init(device);
    test.run("rayGenShaderReorderHitObj");
}

GPU_TEST_CASE("ray-tracing-reorder-hit-obj-and-hint", ALL)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::ShaderExecutionReordering))
        SKIP("shader execution reordering not supported");

    RayTracingReorderTest test;
    test.init(device);
    test.run("rayGenShaderReorderHitObjAndHint");
}
