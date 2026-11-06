#include "test-ray-tracing-common.h"

#include <memory>

using namespace rhi;
using namespace rhi::testing;

namespace {

struct TriangleBuild
{
    ComPtr<IBuffer> vertices;
    AccelerationStructureBuildInput input = {};
    AccelerationStructureBuildDesc desc = {};
    AccelerationStructureSizes sizes = {};

    TriangleBuild(IDevice* device, float x, float z)
    {
        Vertex data[] = {{x, 0.f, z}, {x + 1.f, 0.f, z}, {x, 1.f, z}};
        BufferDesc bufferDesc = {};
        bufferDesc.size = sizeof(data);
        bufferDesc.usage = BufferUsage::AccelerationStructureBuildInput | BufferUsage::CopyDestination;
        bufferDesc.defaultState = ResourceState::AccelerationStructureBuildInput;
        vertices = device->createBuffer(bufferDesc, data);
        REQUIRE(vertices);
        input.type = AccelerationStructureBuildInputType::Triangles;
        input.triangles.vertexBuffers[0] = vertices;
        input.triangles.vertexBufferCount = 1;
        input.triangles.vertexFormat = Format::RGB32Float;
        input.triangles.vertexCount = 3;
        input.triangles.vertexStride = sizeof(Vertex);
        input.triangles.flags = AccelerationStructureGeometryFlags::Opaque;
        desc.inputs = &input;
        desc.inputCount = 1;
        desc.flags = AccelerationStructureBuildFlags::AllowUpdate | AccelerationStructureBuildFlags::AllowCompaction;
        REQUIRE_CALL(device->getAccelerationStructureSizes(desc, &sizes));
    }
};

ComPtr<IBuffer> makeStorage(IDevice* device, Size size, BufferUsage usage)
{
    BufferDesc desc = {};
    desc.size = size;
    desc.usage = usage;
    desc.defaultState = usage == BufferUsage::AccelerationStructure ? ResourceState::AccelerationStructureRead
                                                                    : ResourceState::UnorderedAccess;
    auto buffer = device->createBuffer(desc);
    REQUIRE(buffer);
    return buffer;
}

ComPtr<IAccelerationStructure> makeAS(IDevice* device, Size size, IBuffer* buffer = nullptr, Size offset = 0)
{
    AccelerationStructureDesc desc = {};
    desc.kind = AccelerationStructureKind::BottomLevel;
    desc.size = size;
    desc.buffer = buffer;
    desc.offset = offset;
    ComPtr<IAccelerationStructure> structure;
    REQUIRE_CALL(device->createAccelerationStructure(desc, structure.writeRef()));
    REQUIRE(structure);
    CHECK(structure->getDesc().buffer == buffer);
    CHECK(structure->getDesc().offset == offset);
    return structure;
}

void tracePair(
    IDevice* device,
    ICommandQueue* queue,
    IAccelerationStructure* first,
    IAccelerationStructure* second,
    float firstDistance,
    float secondDistance
)
{
    const auto type = getAccelerationStructureInstanceDescType(device);
    const Size stride = getAccelerationStructureInstanceDescSize(type);
    std::vector<uint8_t> native(2 * stride);
    for (uint32_t i = 0; i < 2; ++i)
    {
        AccelerationStructureInstanceDescGeneric instance = {};
        instance.transform[0][0] = instance.transform[1][1] = instance.transform[2][2] = 1.f;
        instance.instanceMask = 0xff;
        instance.accelerationStructure = (i ? second : first)->getHandle();
        convertAccelerationStructureInstanceDesc(type, native.data() + i * stride, &instance);
    }
    BufferDesc instanceDesc = {};
    instanceDesc.size = native.size();
    instanceDesc.usage = BufferUsage::AccelerationStructureBuildInput;
    instanceDesc.defaultState = ResourceState::AccelerationStructureBuildInput;
    auto instances = device->createBuffer(instanceDesc, native.data());
    REQUIRE(instances);
    AccelerationStructureBuildInput input = {};
    input.type = AccelerationStructureBuildInputType::Instances;
    input.instances.instanceBuffer = instances;
    input.instances.instanceCount = 2;
    input.instances.instanceStride = stride;
    AccelerationStructureBuildDesc build = {};
    build.inputs = &input;
    build.inputCount = 1;
    AccelerationStructureSizes sizes = {};
    REQUIRE_CALL(device->getAccelerationStructureSizes(build, &sizes));
    AccelerationStructureDesc sceneDesc = {};
    sceneDesc.kind = AccelerationStructureKind::TopLevel;
    sceneDesc.size = sizes.accelerationStructureSize;
    ComPtr<IAccelerationStructure> scene;
    REQUIRE_CALL(device->createAccelerationStructure(sceneDesc, scene.writeRef()));
    REQUIRE(scene);
    auto scratch = makeStorage(device, sizes.scratchSize, BufferUsage::UnorderedAccess);
    auto results = makeStorage(device, 2 * sizeof(float), BufferUsage::UnorderedAccess | BufferUsage::CopySource);
    RayTracingTestPipeline pipeline(
        device,
        "test-acceleration-structure-placement",
        {"rayGen"},
        {{"closestHit", nullptr, nullptr}},
        {"miss"}
    );
    auto encoder = queue->createCommandEncoder();
    encoder->buildAccelerationStructure(build, scene, nullptr, scratch, 0, nullptr);
    if (device->getDeviceType() == DeviceType::CUDA)
    {
        // OptiX produces the traversable handle at build execution, before it can be bound.
        REQUIRE_CALL(queue->submit(encoder->finish()));
        REQUIRE_CALL(queue->waitOnHost());
        encoder = queue->createCommandEncoder();
    }
    auto pass = encoder->beginRayTracingPass();
    ShaderCursor cursor(pass->bindPipeline(pipeline.raytracingPipeline, pipeline.shaderTable));
    REQUIRE_CALL(cursor["scene"].setBinding(scene));
    REQUIRE_CALL(cursor["results"].setBinding(results));
    pass->dispatchRays(0, 2, 1, 1);
    pass->end();
    REQUIRE_CALL(queue->submit(encoder->finish()));
    REQUIRE_CALL(queue->waitOnHost());
    compareComputeResult(device, results, makeArray<float>(firstDistance, secondDistance));
}

} // namespace

GPU_TEST_CASE("acceleration-structure-buffer-placement", D3D12 | Vulkan | CUDA)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("Ray tracing not supported");
    auto queue = device->getQueue(QueueType::Graphics);
    TriangleBuild firstBuild(device, 0.f, 2.f);
    TriangleBuild secondBuild(device, 2.f, 3.f);
    const auto& limits = device->getInfo().limits;
    REQUIRE_GT(limits.accelerationStructureAlignment, 0);
    REQUIRE_GT(limits.accelerationStructureScratchAlignment, 0);
    Size stride = (firstBuild.sizes.accelerationStructureSize + limits.accelerationStructureAlignment - 1) /
                  limits.accelerationStructureAlignment * limits.accelerationStructureAlignment;
    auto backing = makeStorage(device, 5 * stride, BufferUsage::AccelerationStructure);
    auto first = makeAS(device, firstBuild.sizes.accelerationStructureSize, backing, stride);
    auto second = makeAS(device, secondBuild.sizes.accelerationStructureSize, backing, 2 * stride);
    // Destroying one placed AS must not free the shared allocation.
    {
        auto temporary = makeAS(device, firstBuild.sizes.accelerationStructureSize, backing, 0);
    }
    auto automatic = makeAS(device, firstBuild.sizes.accelerationStructureSize);
    auto clone = makeAS(device, firstBuild.sizes.accelerationStructureSize, backing, 3 * stride);
    IBuffer* retainedBuffer = backing.get();
    backing.setNull(); // AS objects alone now retain the shared allocation.
    CHECK(first->getDesc().buffer == retainedBuffer);
    CHECK(first->getDeviceAddress() != second->getDeviceAddress());
    NativeHandle native = {};
    REQUIRE_CALL(first->getNativeHandle(&native));
    if (device->getDeviceType() == DeviceType::D3D12)
    {
        CHECK_EQ(first->getDeviceAddress(), retainedBuffer->getDeviceAddress() + stride);
        CHECK_EQ(first->getHandle().value, first->getDeviceAddress());
        CHECK_EQ(native.value, first->getDeviceAddress());
    }
    auto scratch = makeStorage(
        device,
        std::max(firstBuild.sizes.scratchSize, firstBuild.sizes.updateScratchSize) +
            limits.accelerationStructureScratchAlignment,
        BufferUsage::UnorderedAccess
    );
    BufferOffsetPair scratchRegion(scratch, limits.accelerationStructureScratchAlignment);
    QueryPoolDesc queryDesc = {};
    queryDesc.type = QueryType::AccelerationStructureCompactedSize;
    queryDesc.count = 1;
    ComPtr<IQueryPool> query;
    REQUIRE_CALL(device->createQueryPool(queryDesc, query.writeRef()));
    REQUIRE(query);
    AccelerationStructureQueryDesc property = {};
    property.queryPool = query;
    property.queryType = queryDesc.type;
    auto encoder = queue->createCommandEncoder();
    encoder->buildAccelerationStructure(firstBuild.desc, first, nullptr, scratchRegion, 1, &property);
    // Consecutive builds reuse scratch and write distinct regions of the same allocation.
    encoder->buildAccelerationStructure(secondBuild.desc, second, nullptr, scratchRegion, 0, nullptr);
    encoder->buildAccelerationStructure(firstBuild.desc, automatic, nullptr, scratchRegion, 0, nullptr);
    REQUIRE_CALL(queue->submit(encoder->finish()));
    REQUIRE_CALL(queue->waitOnHost());
    tracePair(device, queue, first, second, 2.f, 3.f);
    tracePair(device, queue, automatic, second, 2.f, 3.f);

    uint64_t compactedSize = 0;
    REQUIRE_CALL(query->getResult(0, 1, &compactedSize));
    REQUIRE_GT(compactedSize, 0);
    REQUIRE_LE(compactedSize, stride);
    auto compact = makeAS(device, compactedSize, retainedBuffer, 4 * stride);
    encoder = queue->createCommandEncoder();
    encoder->copyAccelerationStructure(compact, first, AccelerationStructureCopyMode::Compact);
    // OptiX clone is not implemented by the existing backend.
    if (device->getDeviceType() != DeviceType::CUDA)
        encoder->copyAccelerationStructure(clone, first, AccelerationStructureCopyMode::Clone);
    REQUIRE_CALL(queue->submit(encoder->finish()));
    REQUIRE_CALL(queue->waitOnHost());
    tracePair(device, queue, compact, second, 2.f, 3.f);
    if (device->getDeviceType() != DeviceType::CUDA)
        tracePair(device, queue, clone, second, 2.f, 3.f);

    TriangleBuild movedBuild(device, 0.f, 4.f);
    encoder = queue->createCommandEncoder();
    encoder->buildAccelerationStructure(movedBuild.desc, first, nullptr, scratchRegion, 0, nullptr);
    REQUIRE_CALL(queue->submit(encoder->finish()));
    REQUIRE_CALL(queue->waitOnHost());
    tracePair(device, queue, first, second, 4.f, 3.f);
    TriangleBuild updateBuild(device, 0.f, 5.f);
    updateBuild.desc.mode = AccelerationStructureBuildMode::Update;
    encoder = queue->createCommandEncoder();
    encoder->buildAccelerationStructure(updateBuild.desc, first, first, scratchRegion, 0, nullptr);
    REQUIRE_CALL(queue->submit(encoder->finish()));
    REQUIRE_CALL(queue->waitOnHost());
    tracePair(device, queue, first, second, 5.f, 3.f);
    tracePair(device, queue, compact, second, 2.f, 3.f);
    if (device->getDeviceType() != DeviceType::CUDA)
    {
        // Out-of-place update reads and writes distinct regions of the same buffer.
        encoder = queue->createCommandEncoder();
        encoder->buildAccelerationStructure(updateBuild.desc, clone, first, scratchRegion, 0, nullptr);
        REQUIRE_CALL(queue->submit(encoder->finish()));
        REQUIRE_CALL(queue->waitOnHost());
        tracePair(device, queue, clone, second, 5.f, 3.f);
    }

    // Recorded commands retain both AS and storage even after all application references are dropped.
    encoder = queue->createCommandEncoder();
    encoder->buildAccelerationStructure(firstBuild.desc, first, nullptr, scratchRegion, 0, nullptr);
    auto commands = encoder->finish();
    first.setNull();
    second.setNull();
    compact.setNull();
    clone.setNull();
    scratch.setNull();
    REQUIRE_CALL(queue->submit(commands));
    commands.setNull();
    REQUIRE_CALL(queue->waitOnHost());
}

namespace {
struct PlacementDiagnostics : IDebugCallback
{
    uint32_t errors = 0;
    void SLANG_MCALL handleMessage(DebugMessageType type, DebugMessageSource, const char*) override
    {
        if (type == DebugMessageType::Error)
            ++errors;
    }
};
} // namespace

GPU_TEST_CASE("acceleration-structure-buffer-placement-validation", D3D12 | Vulkan | CUDA | Metal | DontCreateDevice)
{
    PlacementDiagnostics diagnostics;
    DeviceExtraOptions options;
    options.debugCallback = &diagnostics;
    device = createTestingDevice(ctx, ctx->deviceType, false, &options);
    if (!device || !device->hasFeature(Feature::AccelerationStructure))
        SKIP("Acceleration structures not supported");
    auto backing = makeStorage(device, 4096, BufferUsage::AccelerationStructure);
    AccelerationStructureDesc desc = {};
    desc.kind = AccelerationStructureKind::BottomLevel;
    desc.size = 256;
    desc.buffer = backing;
    auto reject = [&]()
    {
        ComPtr<IAccelerationStructure> structure;
        uint32_t before = diagnostics.errors;
        CHECK(SLANG_FAILED(device->createAccelerationStructure(desc, structure.writeRef())));
        CHECK_FALSE(structure);
        CHECK_GT(diagnostics.errors, before);
    };
    if (!device->getInfo().limits.accelerationStructureAlignment)
    {
        reject();
        return;
    }
    desc.size = 0;
    reject();
    desc.size = 256;
    desc.offset = 1;
    reject();
    desc.offset = 4096;
    reject();
    desc.offset = UINT64_MAX - 255;
    reject();
    desc.offset = 256;
    desc.size = UINT64_MAX;
    reject();
    desc.size = 256;
    desc.buffer = nullptr;
    reject();
    auto wrongUsage = makeStorage(device, 4096, BufferUsage::UnorderedAccess);
    desc.buffer = wrongUsage;
    desc.offset = 0;
    reject();
    if (device->getDeviceType() == DeviceType::D3D12)
    {
        BufferDesc wrongStateDesc = {};
        wrongStateDesc.size = 4096;
        wrongStateDesc.usage = BufferUsage::AccelerationStructure;
        wrongStateDesc.defaultState = ResourceState::UnorderedAccess;
        auto wrongState = device->createBuffer(wrongStateDesc);
        REQUIRE(wrongState);
        desc.buffer = wrongState;
        reject();
    }
    // A region ending exactly at the buffer boundary is valid.
    desc.buffer = backing;
    desc.offset = 4096 - desc.size;
    ComPtr<IAccelerationStructure> boundary;
    REQUIRE_CALL(device->createAccelerationStructure(desc, boundary.writeRef()));
    REQUIRE(boundary);
    desc.offset = 0;
    auto otherDevice = createTestingDevice(ctx, ctx->deviceType, false, &options);
    REQUIRE(otherDevice);
    auto foreign = makeStorage(otherDevice, 4096, BufferUsage::AccelerationStructure);
    desc.buffer = foreign;
    reject();
}

GPU_TEST_CASE("acceleration-structure-buffer-placement-frame-history", D3D12 | Vulkan | CUDA)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("Ray tracing not supported");

    constexpr uint32_t kStaticCount = 2;
    constexpr uint32_t kDynamicCount = 3;
    constexpr uint32_t kMeshCount = kStaticCount + kDynamicCount;
    constexpr uint32_t kRayCount = kMeshCount + 1; // Include a ray that misses every mesh.
    constexpr uint32_t kFrameCount = 10;
    auto queue = device->getQueue(QueueType::Graphics);
    const auto& limits = device->getInfo().limits;
    REQUIRE_GT(limits.accelerationStructureAlignment, 0);
    REQUIRE_GT(limits.accelerationStructureScratchAlignment, 0);
    auto alignStorage = [&](Size size)
    {
        Size alignment = limits.accelerationStructureAlignment;
        return (size + alignment - 1) / alignment * alignment;
    };

    std::array<std::unique_ptr<TriangleBuild>, kStaticCount> staticBuilds;
    std::array<std::array<std::unique_ptr<TriangleBuild>, kDynamicCount>, 2> dynamicBuilds;
    for (uint32_t mesh = 0; mesh < kStaticCount; ++mesh)
        staticBuilds[mesh] = std::make_unique<TriangleBuild>(device, 2.f * mesh, 1.f + 0.5f * mesh);
    for (auto& slot : dynamicBuilds)
        for (uint32_t mesh = 0; mesh < kDynamicCount; ++mesh)
            slot[mesh] = std::make_unique<TriangleBuild>(device, 2.f * (kStaticCount + mesh), 2.f);
    const auto& blasSizes = staticBuilds[0]->sizes;

    const auto instanceType = getAccelerationStructureInstanceDescType(device);
    const Size instanceStride = getAccelerationStructureInstanceDescSize(instanceType);
    std::array<ComPtr<IBuffer>, 2> instanceBuffers;
    BufferDesc instanceDesc = {};
    instanceDesc.size = kMeshCount * instanceStride;
    instanceDesc.usage = BufferUsage::AccelerationStructureBuildInput | BufferUsage::CopyDestination;
    instanceDesc.defaultState = ResourceState::AccelerationStructureBuildInput;
    for (auto& buffer : instanceBuffers)
    {
        buffer = device->createBuffer(instanceDesc);
        REQUIRE(buffer);
    }
    AccelerationStructureBuildInput sceneInput = {};
    sceneInput.type = AccelerationStructureBuildInputType::Instances;
    sceneInput.instances.instanceBuffer = instanceBuffers[0];
    sceneInput.instances.instanceCount = kMeshCount;
    sceneInput.instances.instanceStride = instanceStride;
    AccelerationStructureBuildDesc sceneBuild = {};
    sceneBuild.inputs = &sceneInput;
    sceneBuild.inputCount = 1;
    sceneBuild.flags = AccelerationStructureBuildFlags::AllowUpdate;
    AccelerationStructureSizes sceneSizes = {};
    REQUIRE_CALL(device->getAccelerationStructureSizes(sceneBuild, &sceneSizes));

    // Interleave both frame slots in one allocation: updating one slot must preserve
    // static geometry and the other slot, even though all of them share buffer barriers.
    Size storageSize = limits.accelerationStructureAlignment +
                       (kStaticCount + 2 * kDynamicCount) * alignStorage(blasSizes.accelerationStructureSize) +
                       2 * alignStorage(sceneSizes.accelerationStructureSize);
    auto backing = makeStorage(device, storageSize, BufferUsage::AccelerationStructure);
    Size offset = limits.accelerationStructureAlignment;
    auto place = [&](AccelerationStructureKind kind, Size size)
    {
        AccelerationStructureDesc desc = {};
        desc.kind = kind;
        desc.size = size;
        desc.buffer = backing;
        desc.offset = offset;
        ComPtr<IAccelerationStructure> structure;
        REQUIRE_CALL(device->createAccelerationStructure(desc, structure.writeRef()));
        REQUIRE(structure);
        offset += alignStorage(size);
        return structure;
    };
    std::array<ComPtr<IAccelerationStructure>, kStaticCount> staticBlas;
    std::array<std::array<ComPtr<IAccelerationStructure>, kDynamicCount>, 2> dynamicBlas;
    std::array<ComPtr<IAccelerationStructure>, 2> scenes;
    for (auto& structure : staticBlas)
        structure = place(AccelerationStructureKind::BottomLevel, blasSizes.accelerationStructureSize);
    for (uint32_t mesh = 0; mesh < kDynamicCount; ++mesh)
        for (uint32_t slot = 0; slot < 2; ++slot)
            dynamicBlas[slot][mesh] =
                place(AccelerationStructureKind::BottomLevel, blasSizes.accelerationStructureSize);
    for (auto& scene : scenes)
        scene = place(AccelerationStructureKind::TopLevel, sceneSizes.accelerationStructureSize);
    CHECK_EQ(offset, storageSize);
    backing.setNull(); // Only AS objects retain the shared storage throughout the frame loop.

    Size scratchSize = std::max(
        {blasSizes.scratchSize, blasSizes.updateScratchSize, sceneSizes.scratchSize, sceneSizes.updateScratchSize}
    );
    auto scratch =
        makeStorage(device, scratchSize + limits.accelerationStructureScratchAlignment, BufferUsage::UnorderedAccess);
    BufferOffsetPair scratchRegion(scratch, limits.accelerationStructureScratchAlignment);
    RayTracingTestPipeline pipeline(
        device,
        "test-acceleration-structure-placement-history",
        {"rayGen"},
        {{"closestHit", nullptr, nullptr}},
        {"miss"}
    );
    auto results =
        makeStorage(device, 2 * kRayCount * sizeof(float), BufferUsage::UnorderedAccess | BufferUsage::CopySource);
    std::array<std::array<float, kRayCount>, 2> expected = {};

    // Static BLASes are built exactly once and referenced by both TLASes for every frame.
    auto encoder = queue->createCommandEncoder();
    for (uint32_t mesh = 0; mesh < kStaticCount; ++mesh)
        encoder->buildAccelerationStructure(
            staticBuilds[mesh]->desc,
            staticBlas[mesh],
            nullptr,
            scratchRegion,
            0,
            nullptr
        );
    REQUIRE_CALL(queue->submit(encoder->finish()));
    REQUIRE_CALL(queue->waitOnHost());

    for (uint32_t frame = 0; frame < kFrameCount; ++frame)
    {
        CAPTURE(frame);
        uint32_t current = frame % 2;
        uint32_t previous = current ^ 1;
        bool refit = frame >= 2;
        for (uint32_t mesh = 0; mesh < kStaticCount; ++mesh)
            expected[current][mesh] = 1.f + 0.5f * mesh;
        expected[current][kMeshCount] = -1.f;

        encoder = queue->createCommandEncoder();
        std::array<AccelerationStructureHandle, kDynamicCount> oldHandles;
        for (uint32_t mesh = 0; mesh < kDynamicCount; ++mesh)
        {
            auto& build = *dynamicBuilds[current][mesh];
            auto& structure = dynamicBlas[current][mesh];
            oldHandles[mesh] = structure->getHandle();
            float x = 2.f * (kStaticCount + mesh);
            float z = 2.f + 0.375f * frame + 0.5f * mesh;
            float slope = 0.125f * (frame % 3);
            // Deform the vertices, rather than moving the TLAS instance transform.
            const Vertex vertices[] = {{x, 0.f, z}, {x + 1.f, 0.f, z + slope}, {x, 1.f, z + 2.f * slope}};
            REQUIRE_CALL(encoder->uploadBufferData(build.vertices, 0, sizeof(vertices), vertices));
            build.desc.mode = refit ? AccelerationStructureBuildMode::Update : AccelerationStructureBuildMode::Build;
            encoder->buildAccelerationStructure(
                build.desc,
                structure,
                refit ? structure.get() : nullptr,
                scratchRegion,
                0,
                nullptr
            );
            // The ray hits at barycentrics (0.25, 0.25), so interpolate vertex Z analytically.
            expected[current][kStaticCount + mesh] = z + 0.75f * slope;
        }
        REQUIRE_CALL(queue->submit(encoder->finish()));
        REQUIRE_CALL(queue->waitOnHost());
        if (refit)
            for (uint32_t mesh = 0; mesh < kDynamicCount; ++mesh)
                CHECK_EQ(dynamicBlas[current][mesh]->getHandle().value, oldHandles[mesh].value);

        // OptiX handles are available after BLAS execution. The previous TLAS and its
        // BLAS slot are left untouched; only the slot last used two frames ago is reused.
        std::vector<uint8_t> nativeInstances(kMeshCount * instanceStride);
        for (uint32_t mesh = 0; mesh < kMeshCount; ++mesh)
        {
            AccelerationStructureInstanceDescGeneric instance = {};
            instance.transform[0][0] = instance.transform[1][1] = instance.transform[2][2] = 1.f;
            instance.instanceID = mesh;
            instance.instanceMask = 0xff;
            instance.accelerationStructure = mesh < kStaticCount
                                                 ? staticBlas[mesh]->getHandle()
                                                 : dynamicBlas[current][mesh - kStaticCount]->getHandle();
            convertAccelerationStructureInstanceDesc(
                instanceType,
                nativeInstances.data() + mesh * instanceStride,
                &instance
            );
        }
        encoder = queue->createCommandEncoder();
        REQUIRE_CALL(
            encoder->uploadBufferData(instanceBuffers[current], 0, nativeInstances.size(), nativeInstances.data())
        );
        sceneInput.instances.instanceBuffer = instanceBuffers[current];
        sceneBuild.mode = refit ? AccelerationStructureBuildMode::Update : AccelerationStructureBuildMode::Build;
        auto oldSceneHandle = scenes[current]->getHandle();
        encoder->buildAccelerationStructure(
            sceneBuild,
            scenes[current],
            refit ? scenes[current].get() : nullptr,
            scratchRegion,
            0,
            nullptr
        );
        REQUIRE_CALL(queue->submit(encoder->finish()));
        REQUIRE_CALL(queue->waitOnHost());
        if (refit)
            CHECK_EQ(scenes[current]->getHandle().value, oldSceneHandle.value);

        // Bind both TLASes together: every ray invocation traces the current and previous
        // scenes, catching binding aliasing as well as accidental writes to retained geometry.
        // Frame zero has no history yet, so use the current scene for both bindings.
        uint32_t history = frame > 0 ? previous : current;
        encoder = queue->createCommandEncoder();
        auto pass = encoder->beginRayTracingPass();
        ShaderCursor cursor(pass->bindPipeline(pipeline.raytracingPipeline, pipeline.shaderTable));
        REQUIRE_CALL(cursor["currentScene"].setBinding(scenes[current]));
        REQUIRE_CALL(cursor["previousScene"].setBinding(scenes[history]));
        REQUIRE_CALL(cursor["results"].setBinding(results));
        pass->dispatchRays(0, kRayCount, 1, 1);
        pass->end();
        REQUIRE_CALL(queue->submit(encoder->finish()));
        REQUIRE_CALL(queue->waitOnHost());
        std::array<float, 2 * kRayCount> expectedPairs = {};
        for (uint32_t ray = 0; ray < kRayCount; ++ray)
        {
            expectedPairs[2 * ray] = expected[current][ray];
            expectedPairs[2 * ray + 1] = expected[history][ray];
        }
        compareComputeResult(device, results, expectedPairs);
    }
}
