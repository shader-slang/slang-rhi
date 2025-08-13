#include "testing.h"
#include "texture-utils.h"

#include <slang-rhi/acceleration-structure-utils.h>

using namespace rhi;
using namespace rhi::testing;

struct float3
{
    float x, y, z;
};

struct RayTracingLssTestBase
{
    IDevice* device;

    ComPtr<ICommandQueue> queue;

    ComPtr<IRayTracingPipeline> raytracingPipeline;
    ComPtr<IBuffer> positionBuffer;
    ComPtr<IBuffer> radiusBuffer;
    ComPtr<IBuffer> indexBuffer;
    ComPtr<IBuffer> transformBuffer;
    ComPtr<IBuffer> instanceBuffer;
    ComPtr<IBuffer> BLASBuffer;
    ComPtr<IAccelerationStructure> BLAS;
    ComPtr<IBuffer> TLASBuffer;
    ComPtr<IAccelerationStructure> TLAS;
    ComPtr<IShaderTable> shaderTable;

    void init(IDevice* device_) { this->device = device_; }

    // Load and compile shader code from source.
    Result loadShaderProgram(span<const char*> entryPointNames, IShaderProgram** outProgram)
    {
        ComPtr<slang::ISession> slangSession;
        slangSession = device->getSlangSession();

        ComPtr<slang::IBlob> diagnosticsBlob;
        slang::IModule* module = slangSession->loadModule("test-ray-tracing-lss", diagnosticsBlob.writeRef());
        diagnoseIfNeeded(diagnosticsBlob);
        if (!module)
            return SLANG_FAIL;

        std::vector<slang::IComponentType*> componentTypes;
        componentTypes.push_back(module);

        ComPtr<slang::IEntryPoint> entryPoint;
        for (const char* entryPointName : entryPointNames)
        {
            SLANG_RETURN_ON_FAIL(module->findEntryPointByName(entryPointName, entryPoint.writeRef()));
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

    void createRequiredResources(
        unsigned primitiveCount,
        unsigned segmentCount,
        const float3* positions,
        const float* radii,
        const unsigned* indices,
        const char* raygenName,
        const char* closestHitName,
        const char* missName
    )
    {
        queue = device->getQueue(QueueType::Graphics);

        BufferDesc positionBufferDesc;
        positionBufferDesc.size = segmentCount * sizeof(float3);
        positionBufferDesc.usage = BufferUsage::AccelerationStructureBuildInput;
        positionBufferDesc.defaultState = ResourceState::AccelerationStructureBuildInput;
        positionBuffer = device->createBuffer(positionBufferDesc, positions);
        REQUIRE(positionBuffer != nullptr);

        BufferDesc radiusBufferDesc;
        radiusBufferDesc.size = segmentCount * sizeof(float);
        radiusBufferDesc.usage = BufferUsage::AccelerationStructureBuildInput;
        radiusBufferDesc.defaultState = ResourceState::AccelerationStructureBuildInput;
        radiusBuffer = device->createBuffer(radiusBufferDesc, radii);
        REQUIRE(radiusBuffer != nullptr);

        BufferDesc indexBufferDesc;
        indexBufferDesc.size = primitiveCount * sizeof(unsigned);
        indexBufferDesc.usage = BufferUsage::AccelerationStructureBuildInput;
        indexBufferDesc.defaultState = ResourceState::AccelerationStructureBuildInput;
        indexBuffer = device->createBuffer(indexBufferDesc, indices);
        REQUIRE(indexBuffer != nullptr);

        // Build bottom level acceleration structure.
        {
            AccelerationStructureBuildInput buildInput = {};
            buildInput.type = AccelerationStructureBuildInputType::LinearSweptSpheres;
            buildInput.linearSweptSpheres.primitiveCount = primitiveCount;
            buildInput.linearSweptSpheres.vertexBufferCount = 1;
            buildInput.linearSweptSpheres.vertexCount = segmentCount;
            buildInput.linearSweptSpheres.vertexPositionBuffers[0] = positionBuffer;
            buildInput.linearSweptSpheres.vertexPositionFormat = Format::RGB32Float;
            buildInput.linearSweptSpheres.vertexPositionStride = sizeof(float3);
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

        const char* entryPointNames[] = {raygenName, missName, closestHitName};

        ComPtr<IShaderProgram> rayTracingProgram;
        REQUIRE_CALL(loadShaderProgram(entryPointNames, rayTracingProgram.writeRef()));

        HitGroupDesc hitGroups[1];
        hitGroups[0].hitGroupName = hitgroupNames[0];
        hitGroups[0].closestHitEntryPoint = closestHitName;

        // We must specify an explicit intersection shader for all non-triangle geometry in OptiX.
        if (device->getDeviceType() == DeviceType::CUDA)
            hitGroups[0].intersectionEntryPoint = "__builtin_intersection__linear_swept_spheres";

        RayTracingPipelineDesc rtpDesc = {};
        rtpDesc.program = rayTracingProgram;
        rtpDesc.hitGroupCount = 1;
        rtpDesc.hitGroups = hitGroups;
        rtpDesc.maxRayPayloadSize = 64;
        rtpDesc.maxAttributeSizeInBytes = 8;
        rtpDesc.maxRecursion = 2;
        rtpDesc.flags = RayTracingPipelineFlags::EnableLinearSweptSpheres;
        REQUIRE_CALL(device->createRayTracingPipeline(rtpDesc, raytracingPipeline.writeRef()));
        REQUIRE(raytracingPipeline != nullptr);

        ShaderTableDesc shaderTableDesc = {};
        shaderTableDesc.program = rayTracingProgram;
        shaderTableDesc.hitGroupCount = 1;
        shaderTableDesc.hitGroupNames = hitgroupNames;
        shaderTableDesc.rayGenShaderCount = 1;
        shaderTableDesc.rayGenShaderEntryPointNames = &raygenName;
        shaderTableDesc.missShaderCount = 1;
        shaderTableDesc.missShaderEntryPointNames = &missName;
        REQUIRE_CALL(device->createShaderTable(shaderTableDesc, shaderTable.writeRef()));
    }
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

// Test that the ray tracing pipeline can perform sphere intersection.
struct RayTracingLssTest : public RayTracingLssTestBase
{
    static constexpr int kVertexCount = 3;
    static constexpr int kPrimitiveCount = 2;

    static constexpr float3 kPositions[kVertexCount] = {
        {-0.5f, -0.5f, 3.0f},
        {0.0, 0.5f, 3.0f},
        {0.5f, -0.5f, 3.0f},
    };

    static constexpr float kRadii[kVertexCount] = {0.2f, 0.2f, 0.2f};

    static constexpr unsigned kIndices[kPrimitiveCount] = {0, 1};

    ComPtr<ITexture> resultTexture;

    uint32_t width = 128;
    uint32_t height = 128;

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

    void checkTestResults(span<ExpectedPixel> expectedPixels)
    {
        ComPtr<ISlangBlob> resultBlob;
        SubresourceLayout layout;
        REQUIRE_CALL(device->readTexture(resultTexture, 0, 0, resultBlob.writeRef(), &layout));
#if 0 // for debugging only
        writeImage("test-ray-tracing-lss-intersection.hdr", resultBlob, width, height, layout.rowPitch, layout.colPitch);
#endif

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

    void run()
    {
        createRequiredResources(
            kPrimitiveCount,
            kVertexCount,
            kPositions,
            kRadii,
            kIndices,
            "rayGenShader",
            "closestHitShader",
            "missShader"
        );

        createResultTexture();
        renderFrame();

        ExpectedPixel expectedPixels[] = {
            EXPECTED_PIXEL(32, 32, 1.f, 0.f, 0.f, 1.f), // Segment 1, top left
            EXPECTED_PIXEL(96, 32, 0.f, 1.f, 0.f, 1.f), // Segment 2, top right

            // Corners should all be misses
            EXPECTED_PIXEL(0, 0, 1.f, 1.0f, 1.0f, 1.0f),     // Miss
            EXPECTED_PIXEL(127, 0, 1.f, 1.0f, 1.0f, 1.0f),   // Miss
            EXPECTED_PIXEL(127, 127, 1.f, 1.0f, 1.0f, 1.0f), // Miss
            EXPECTED_PIXEL(0, 127, 1.f, 1.0f, 1.0f, 1.0f),   // Miss

            // Center between segments should be a miss
            EXPECTED_PIXEL(64, 32, 1.f, 1.0f, 1.0f, 1.0f),
        };
        checkTestResults(expectedPixels);
    }
};

GPU_TEST_CASE("ray-tracing-lss-intersection", ALL)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::AccelerationStructureLinearSweptSpheres))
        SKIP("acceleration structure linear swept spheres not supported");

    RayTracingLssTest test;
    test.init(device);
    test.run();
}

struct TestResult
{
    int isLssHit;
    float lssPositionsAndRadii[8];
};

struct TestResultCudaAligned
{
    int isLssHit;
    int pad[3];
    float lssPositionsAndRadii[8];
};

struct RayTracingLssIntrinsicsTest : public RayTracingLssTestBase
{
    static constexpr int kVertexCount = 2;
    static constexpr int kPrimitiveCount = 1;

    static constexpr float3 kPositions[kVertexCount] = {
        {-0.5f, 0.0f, -3.0f},
        {0.5, 0.0f, -3.0f},
    };

    static constexpr float kRadii[kVertexCount] = {0.5f, 0.5f};

    static constexpr unsigned kIndices[kPrimitiveCount] = {0};


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

    template<typename T>
    void checkTestResults()
    {
        ComPtr<ISlangBlob> resultBlob;
        REQUIRE_CALL(device->readBuffer(resultBuffer, 0, sizeof(T), resultBlob.writeRef()));

        const T* result = reinterpret_cast<const T*>(resultBlob->getBufferPointer());

        CHECK_EQ(result->isLssHit, 1);

        // Left endcap position
        CHECK_EQ(result->lssPositionsAndRadii[0], -0.5f);
        CHECK_EQ(result->lssPositionsAndRadii[1], 0.0f);
        CHECK_EQ(result->lssPositionsAndRadii[2], -3.0f);

        // Left endcap radius
        CHECK_EQ(result->lssPositionsAndRadii[3], 0.5f);

        // Right endcap position
        CHECK_EQ(result->lssPositionsAndRadii[4], 0.5f);
        CHECK_EQ(result->lssPositionsAndRadii[5], 0.0f);
        CHECK_EQ(result->lssPositionsAndRadii[6], -3.0f);

        // Right endcap radius
        CHECK_EQ(result->lssPositionsAndRadii[7], 0.5f);
    }

    void checkTestResults()
    {
        if (device->getDeviceType() == DeviceType::CUDA)
            checkTestResults<TestResultCudaAligned>();
        else
            checkTestResults<TestResult>();
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

    void run(const char* raygenName, const char* closestHitName)
    {
        createRequiredResources(
            kPrimitiveCount,
            kVertexCount,
            kPositions,
            kRadii,
            kIndices,
            raygenName,
            closestHitName,
            "missNOP"
        );
        createResultBuffer();
        renderFrame();
        checkTestResults();
    }
};

GPU_TEST_CASE("ray-tracing-lss-intrinsics", ALL)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::AccelerationStructureLinearSweptSpheres))
        SKIP("acceleration structure linear swept spheres not supported");

    RayTracingLssIntrinsicsTest test;
    test.init(device);
    test.run("rayGenLssIntrinsics", "closestHitLssIntrinsics");
}

// Disabled under D3D12 due to https://github.com/shader-slang/slang/issues/8128
GPU_TEST_CASE("ray-tracing-lss-intrinsics-hit-object", ALL & ~D3D12)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::AccelerationStructureLinearSweptSpheres))
        SKIP("acceleration structure linear swept spheres not supported");

    RayTracingLssIntrinsicsTest test;
    test.init(device);
    test.run("rayGenLssIntrinsicsHitObject", "closestHitNOP");
}
