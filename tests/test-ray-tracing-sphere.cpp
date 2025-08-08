#include "testing.h"
#include "texture-utils.h"

#include <slang-rhi/acceleration-structure-utils.h>

using namespace rhi;
using namespace rhi::testing;

struct float3
{
    float x, y, z;
};

struct RayTracingSphereTestBase
{
    IDevice* device;

    ComPtr<ICommandQueue> queue;

    ComPtr<IRayTracingPipeline> raytracingPipeline;
    ComPtr<IBuffer> positionBuffer;
    ComPtr<IBuffer> radiusBuffer;
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
        slang::IModule* module = slangSession->loadModule("test-ray-tracing-sphere", diagnosticsBlob.writeRef());
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
        unsigned sphereCount,
        const float3* positions,
        const float* radii,
        const char* raygenName,
        const char* closestHitName,
        const char* missName
    )
    {
        queue = device->getQueue(QueueType::Graphics);

        BufferDesc positionBufferDesc;
        positionBufferDesc.size = sphereCount * sizeof(float3);
        positionBufferDesc.usage = BufferUsage::AccelerationStructureBuildInput;
        positionBufferDesc.defaultState = ResourceState::AccelerationStructureBuildInput;
        positionBuffer = device->createBuffer(positionBufferDesc, positions);
        REQUIRE(positionBuffer != nullptr);

        BufferDesc radiusBufferDesc;
        radiusBufferDesc.size = sphereCount * sizeof(float);
        radiusBufferDesc.usage = BufferUsage::AccelerationStructureBuildInput;
        radiusBufferDesc.defaultState = ResourceState::AccelerationStructureBuildInput;
        radiusBuffer = device->createBuffer(radiusBufferDesc, radii);
        REQUIRE(radiusBuffer != nullptr);

        // Build bottom level acceleration structure.
        {
            AccelerationStructureBuildInput buildInput = {};
            buildInput.type = AccelerationStructureBuildInputType::Spheres;
            buildInput.spheres.vertexBufferCount = 1;
            buildInput.spheres.vertexCount = sphereCount;
            buildInput.spheres.vertexPositionBuffers[0] = positionBuffer;
            buildInput.spheres.vertexPositionFormat = Format::RGB32Float;
            buildInput.spheres.vertexPositionStride = sizeof(float3);
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
            hitGroups[0].intersectionEntryPoint = "__builtin_intersection__sphere";

        RayTracingPipelineDesc rtpDesc = {};
        rtpDesc.program = rayTracingProgram;
        rtpDesc.hitGroupCount = 1;
        rtpDesc.hitGroups = hitGroups;
        rtpDesc.maxRayPayloadSize = 64;
        rtpDesc.maxAttributeSizeInBytes = 8;
        rtpDesc.maxRecursion = 2;
        rtpDesc.flags = RayTracingPipelineFlags::EnableSpheres;
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
struct RayTracingSphereIntersectionTest : public RayTracingSphereTestBase
{
    static constexpr int kSphereCount = 3;

    static constexpr float3 kPositions[kSphereCount] = {
        {-0.5f, -0.5f, 3.0f},
        {0.5, -0.5f, 3.0f},
        {0.0f, 0.5f, 3.0f},
    };

    static constexpr float kRadii[kSphereCount] = {0.4f, 0.2f, 0.6f};

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
        writeImage("test-ray-tracing-sphere-intersection.hdr", resultBlob, width, height, layout.rowPitch, layout.colPitch);
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
        createRequiredResources(kSphereCount, kPositions, kRadii, "rayGenShader", "closestHitShader", "missShader");
        createResultTexture();
        renderFrame();

        ExpectedPixel expectedPixels[] = {
            EXPECTED_PIXEL(32, 32, 1.f, 0.f, 0.f, 1.f), // Sphere 1
            EXPECTED_PIXEL(96, 32, 0.f, 1.f, 0.f, 1.f), // Sphere 2
            EXPECTED_PIXEL(64, 96, 0.f, 0.f, 1.f, 1.f), // Sphere 3

            // Corners should all be misses
            EXPECTED_PIXEL(0, 0, 1.f, 1.0f, 1.0f, 1.0f),     // Miss
            EXPECTED_PIXEL(127, 0, 1.f, 1.0f, 1.0f, 1.0f),   // Miss
            EXPECTED_PIXEL(127, 127, 1.f, 1.0f, 1.0f, 1.0f), // Miss
            EXPECTED_PIXEL(0, 127, 1.f, 1.0f, 1.0f, 1.0f),   // Miss
        };
        checkTestResults(expectedPixels);
    }
};

GPU_TEST_CASE("ray-tracing-sphere-intersection", ALL & ~(D3D12 | Vulkan))
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::AccelerationStructureSpheres))
        SKIP("acceleration structure spheres not supported");

    RayTracingSphereIntersectionTest test;
    test.init(device);
    test.run();
}

struct TestResult
{
    int isSphereHit;
    float spherePositionAndRadius[4];
};

struct TestResultCudaAligned
{
    int isSphereHit;
    int pad[3];
    float spherePositionAndRadius[4];
};

struct RayTracingSphereIntrinsicsTest : public RayTracingSphereTestBase
{
    static constexpr int kSphereCount = 1;

    static constexpr float3 kPositions[kSphereCount] = {
        {0.0f, 0.0f, -3.0f},
    };

    static constexpr float kRadii[kSphereCount] = {2.0f};

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
        CHECK_EQ(result->isSphereHit, 1);
        CHECK_EQ(result->spherePositionAndRadius[0], 0.0f);
        CHECK_EQ(result->spherePositionAndRadius[1], 0.0f);
        CHECK_EQ(result->spherePositionAndRadius[2], -3.0f);
        CHECK_EQ(result->spherePositionAndRadius[3], 2.0f);
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
        createRequiredResources(kSphereCount, kPositions, kRadii, raygenName, closestHitName, "missNOP");
        createResultBuffer();
        renderFrame();
        checkTestResults();
    }
};

GPU_TEST_CASE("ray-tracing-sphere-intrinsics", ALL & ~(D3D12 | Vulkan))
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::AccelerationStructureSpheres))
        SKIP("acceleration structure spheres not supported");

    RayTracingSphereIntrinsicsTest test;
    test.init(device);
    test.run("rayGenSphereIntrinsics", "closestHitSphereIntrinsics");
}

GPU_TEST_CASE("ray-tracing-sphere-intrinsics-hit-object", ALL & ~(D3D12 | Vulkan))
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");
    if (!device->hasFeature(Feature::AccelerationStructureSpheres))
        SKIP("acceleration structure spheres not supported");

    RayTracingSphereIntrinsicsTest test;
    test.init(device);
    test.run("rayGenSphereIntrinsicsHitObject", "closestHitNOP");
}
