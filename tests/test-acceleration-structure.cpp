#if 0

#include <vector>
#include <cstdio>
#include <initializer_list>

struct Options {
    bool flagA = false;
    bool flagB = false;
};

template<typename T, bool T::*MemberPtr>
struct FlagOption {
    int value;

    static FlagOption Off() { return FlagOption{0}; }
    static FlagOption On() { return FlagOption{1}; }
    static FlagOption Both() { return FlagOption{2}; }

    template<typename Emit>
    void apply(T options, Emit&& emit) {
        switch (value) {
        case 0:
            options.*MemberPtr = false;
            emit(options);
            break;
        case 1:
            options.*MemberPtr = true;
            emit(options);
            break;
        case 2:
            options.*MemberPtr = false;
            emit(options);
            options.*MemberPtr = true;
            emit(options);
            break;
        }
    }
};

namespace options {

using FlagA = FlagOption<Options, &Options::flagA>;
using FlagB = FlagOption<Options, &Options::flagB>;

}

template<typename T, typename O>
std::vector<T> apply(const std::vector<T>& input, O o) {
    std::vector<T> output;
    for (const auto& in : input) {
        o.apply(in, [&output](const T& out){ output.push_back(out); });
    }
    return output;
}

template<typename T, typename ...Args>
std::vector<T> enumerateCombinations(Args&&... args) {
    std::vector<T> result;
    result.push_back({});
    if constexpr (sizeof...(args) > 0) {
        // result = apply<T>(result, args...);
        // (void)std::initializer_list<int>{
        // (result = apply<T, std::decay_t<Args>>(result, std::forward<Args>(args)), 0)...};
        auto callApply = [&](auto&& arg) {
            using ArgType = std::decay_t<decltype(arg)>;
            result = apply<T, ArgType>(result, std::forward<decltype(arg)>(arg));
        };
        (callApply(std::forward<Args>(args)), ...); // fold expression
    }
    return result;
}

int main() {
    std::vector<Options> tests = enumerateCombinations<Options>(options::FlagA::Both(), options::FlagB::Both());
    for (int i = 0; i < tests.size(); ++i) {
        const Options& options = tests[i];
        printf("test %d\n", i);
        printf("flagA = %s\n", options.flagA ? "on" : "off");
        printf("flagB = %s\n", options.flagB ? "on" : "off");
    }

    return 0;
}

#endif

#include "testing.h"
#include "texture-utils.h"

#include <slang-rhi/acceleration-structure-utils.h>

using namespace rhi;
using namespace rhi::testing;

struct float3
{
    float x, y, z;

    float3()
        : x(0.f)
        , y(0.f)
        , z(0.f)
    {
    }
    float3(float x, float y, float z)
        : x(x)
        , y(y)
        , z(z)
    {
    }

    float3 operator+(const float3& other) const { return float3(x + other.x, y + other.y, z + other.z); }
    float3 operator-(const float3& other) const { return float3(x - other.x, y - other.y, z - other.z); }
    float3 operator*(float scalar) const { return float3(x * scalar, y * scalar, z * scalar); }
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

struct BuildOptions
{
    bool compact = false;
    bool allowUpdate = false;

    bool usePreTransform = false;
    float preTransform[3][4] = {
        {1.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f, 0.0f},
    };
};

namespace options {
enum class Compact
{
    Off,
    On,
    Both,
};

enum class AllowUpdate
{
    Off,
    On,
    Both,
};

enum class PreTransform
{
    Off,
    On,
    Both,
};
}; // namespace options

template<typename... Args>
std::vector<BuildOptions> collectBuildOptions()
{
}

ComPtr<IAccelerationStructure> createAccelerationStructureTriangles(IDevice* device, const BuildOptions& options)
{
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

    BufferDesc vertexBufferDesc;
    vertexBufferDesc.size = kVertexCount * sizeof(Vertex);
    vertexBufferDesc.usage = BufferUsage::AccelerationStructureBuildInput;
    vertexBufferDesc.defaultState = ResourceState::AccelerationStructureBuildInput;
    ComPtr<IBuffer> vertexBuffer;
    REQUIRE_CALL(device->createBuffer(vertexBufferDesc, &kVertexData[0], vertexBuffer.writeRef()));

    BufferDesc indexBufferDesc;
    indexBufferDesc.size = kIndexCount * sizeof(int32_t);
    indexBufferDesc.usage = BufferUsage::AccelerationStructureBuildInput;
    indexBufferDesc.defaultState = ResourceState::AccelerationStructureBuildInput;
    ComPtr<IBuffer> indexBuffer;
    REQUIRE_CALL(device->createBuffer(indexBufferDesc, &kIndexData[0], indexBuffer.writeRef()));

    BufferDesc transformBufferDesc;
    transformBufferDesc.size = sizeof(float) * 12;
    transformBufferDesc.usage = BufferUsage::AccelerationStructureBuildInput;
    transformBufferDesc.defaultState = ResourceState::AccelerationStructureBuildInput;
    ComPtr<IBuffer> transformBuffer;
    REQUIRE_CALL(device->createBuffer(transformBufferDesc, options.preTransform, transformBuffer.writeRef()));

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
    if (options.usePreTransform)
        buildInput.triangles.preTransformBuffer = transformBuffer;
    buildInput.triangles.flags = AccelerationStructureGeometryFlags::Opaque;

    AccelerationStructureBuildDesc buildDesc = {};
    buildDesc.inputs = &buildInput;
    buildDesc.inputCount = 1;
    if (options.allowUpdate)
        buildDesc.flags = AccelerationStructureBuildFlags::AllowUpdate;
    if (options.compact)
        buildDesc.flags = AccelerationStructureBuildFlags::AllowCompaction;

    // Query buffer size for acceleration structure build.
    AccelerationStructureSizes sizes;
    REQUIRE_CALL(device->getAccelerationStructureSizes(buildDesc, &sizes));
    CHECK(sizes.accelerationStructureSize > 0);

    // Allocate buffers for acceleration structure.
    BufferDesc scratchBufferDesc;
    scratchBufferDesc.usage = BufferUsage::UnorderedAccess;
    scratchBufferDesc.defaultState = ResourceState::UnorderedAccess;
    scratchBufferDesc.size = sizes.scratchSize;
    ComPtr<IBuffer> scratchBuffer;
    REQUIRE_CALL(device->createBuffer(scratchBufferDesc, nullptr, scratchBuffer.writeRef()));

    // Create query pool for querying compacted size.
    ComPtr<IQueryPool> queryPool;
    if (options.compact)
    {
        QueryPoolDesc queryPoolDesc;
        queryPoolDesc.count = 1;
        queryPoolDesc.type = QueryType::AccelerationStructureCompactedSize;
        REQUIRE_CALL(device->createQueryPool(queryPoolDesc, queryPool.writeRef()));
        // queryPool->reset();
    }

    // Create acceleration structure.
    ComPtr<IAccelerationStructure> accelerationStructure;
    AccelerationStructureDesc accelerationStructureDesc;
    accelerationStructureDesc.size = sizes.accelerationStructureSize;
    REQUIRE_CALL(device->createAccelerationStructure(accelerationStructureDesc, accelerationStructure.writeRef()));

    // Build acceleration structure.
    ComPtr<ICommandQueue> queue;
    REQUIRE_CALL(device->getQueue(QueueType::Graphics, queue.writeRef()));

    ComPtr<ICommandEncoder> commandEncoder;
    REQUIRE_CALL(queue->createCommandEncoder(commandEncoder.writeRef()));

    AccelerationStructureQueryDesc compactedSizeQueryDesc = {};
    compactedSizeQueryDesc.queryPool = queryPool;
    compactedSizeQueryDesc.queryType = QueryType::AccelerationStructureCompactedSize;
    commandEncoder->buildAccelerationStructure(
        buildDesc,
        accelerationStructure,
        nullptr,
        scratchBuffer,
        options.compact ? 1 : 0,
        options.compact ? &compactedSizeQueryDesc : nullptr
    );
    REQUIRE_CALL(queue->submit(commandEncoder->finish()));
    REQUIRE_CALL(queue->waitOnHost());

    if (options.compact)
    {
        uint64_t compactedSize = 0;
        REQUIRE_CALL(queryPool->getResult(0, 1, &compactedSize));
        AccelerationStructureDesc compactedAccelerationStructureDesc;
        compactedAccelerationStructureDesc.size = compactedSize;
        ComPtr<IAccelerationStructure> compactedAccelerationStructure;
        REQUIRE_CALL(device->createAccelerationStructure(
            compactedAccelerationStructureDesc,
            compactedAccelerationStructure.writeRef()
        ));

        REQUIRE_CALL(queue->createCommandEncoder(commandEncoder.writeRef()));
        commandEncoder->copyAccelerationStructure(
            compactedAccelerationStructure,
            accelerationStructure,
            AccelerationStructureCopyMode::Compact
        );
        REQUIRE_CALL(queue->submit(commandEncoder->finish()));
        REQUIRE_CALL(queue->waitOnHost());

        accelerationStructure = compactedAccelerationStructure;
    }

    return accelerationStructure;
}

ComPtr<IAccelerationStructure> createAccelerationStructureAABBs(IDevice* device) {}

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

    uint32_t width = 128;
    uint32_t height = 128;

    void init(IDevice* device) { this->device = device; }

    // Load and compile shader code from source.
    Result loadShaderProgram(IDevice* device, IShaderProgram** outProgram)
    {
        ComPtr<slang::ISession> slangSession;
        slangSession = device->getSlangSession();

        ComPtr<slang::IBlob> diagnosticsBlob;
        slang::IModule* module = slangSession->loadModule("test-ray-tracing", diagnosticsBlob.writeRef());
        diagnoseIfNeeded(diagnosticsBlob);
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
        resultTextureDesc.mipCount = 1;
        resultTextureDesc.size.width = width;
        resultTextureDesc.size.height = height;
        resultTextureDesc.size.depth = 1;
        resultTextureDesc.usage = TextureUsage::UnorderedAccess | TextureUsage::CopySource;
        resultTextureDesc.defaultState = ResourceState::UnorderedAccess;
        resultTextureDesc.format = Format::RGBA32Float;
        resultTexture = device->createTexture(resultTextureDesc);
    }

    void createRequiredResources()
    {
        queue = device->getQueue(QueueType::Graphics);


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
        rtpDesc.maxAttributeSizeInBytes = 8;
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

    void checkTestResults(span<ExpectedPixel> expectedPixels)
    {
        ComPtr<ISlangBlob> resultBlob;
        SubresourceLayout layout;
        REQUIRE_CALL(device->readTexture(resultTexture, 0, 0, resultBlob.writeRef(), &layout));
#if 0 // for debugging only
        writeImage("test.hdr", resultBlob, width, height, layout.rowPitch, layout.colPitch);
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
};

struct RayTracingTestA : BaseRayTracingTest
{
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
        createRequiredResources();
        renderFrame();

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
        checkTestResults(expectedPixels);
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
        uint32_t dims[2] = {width, height};
        cursor["dims"].setData(dims, sizeof(dims));
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

        ExpectedPixel expectedPixels[] = {
            EXPECTED_PIXEL(64, 64, 0.f, 1.f, 1.f, 1.f), // Triangle 1
            EXPECTED_PIXEL(63, 64, 1.f, 0.f, 1.f, 1.f), // Triangle 2
            EXPECTED_PIXEL(64, 63, 1.f, 1.f, 0.f, 1.f), // Triangle 3
            EXPECTED_PIXEL(63, 63, 0.f, 0.f, 0.f, 1.f), // Miss
            // Corners should all be misses
            EXPECTED_PIXEL(0, 0, 0.f, 0.f, 0.f, 1.f),     // Miss
            EXPECTED_PIXEL(127, 0, 0.f, 0.f, 0.f, 1.f),   // Miss
            EXPECTED_PIXEL(127, 127, 0.f, 0.f, 0.f, 1.f), // Miss
            EXPECTED_PIXEL(0, 127, 0.f, 0.f, 0.f, 1.f),   // Miss
        };
        checkTestResults(expectedPixels);
    }
};

GPU_TEST_CASE("acceleration-structure-triangles", ALL)
{
    if (!device->hasFeature(Feature::RayTracing) || !device->hasFeature(Feature::AccelerationStructure))
        SKIP("ray tracing not supported");
}

GPU_TEST_CASE("ray-tracing-a", ALL)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");

    RayTracingTestA test;
    test.init(device);
    test.run();
}

GPU_TEST_CASE("ray-tracing-b", ALL)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("ray tracing not supported");

    RayTracingTestB test;
    test.init(device);
    test.run();
}
