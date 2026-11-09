#include "test-ray-tracing-common.h"
#include "../src/device.h"
#include "../src/rhi-shared.h"

#include <memory>
#include <functional>

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
    float secondDistance,
    const std::function<void(ICommandEncoder*)>& beforeSceneBuild = {},
    const std::function<void(ICommandEncoder*)>& betweenTraces = {},
    bool rayQuery = false
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
    if (beforeSceneBuild)
        beforeSceneBuild(encoder);
    encoder->buildAccelerationStructure(build, scene, nullptr, scratch, 0, nullptr);
    if (device->getDeviceType() == DeviceType::CUDA)
    {
        // OptiX produces the traversable handle at build execution, before it can be bound.
        REQUIRE_CALL(queue->submit(encoder->finish()));
        REQUIRE_CALL(queue->waitOnHost());
        encoder = queue->createCommandEncoder();
    }
    ComPtr<IComputePipeline> computePipeline;
    if (rayQuery)
    {
        ComPtr<IShaderProgram> program;
        REQUIRE_CALL(
            loadAndLinkProgram(device, "test-acceleration-structure-placement-query", "traceInline", program.writeRef())
        );
        ComputePipelineDesc pipelineDesc = {};
        pipelineDesc.program = program;
        computePipeline = device->createComputePipeline(pipelineDesc);
        REQUIRE(computePipeline);
    }
    ComPtr<IShaderObject> root;
    auto trace = [&]()
    {
        if (rayQuery)
        {
            auto pass = encoder->beginComputePass();
            if (root)
                pass->bindPipeline(computePipeline, root);
            else
            {
                root = pass->bindPipeline(computePipeline);
                ShaderCursor cursor(root);
                REQUIRE_CALL(cursor["scene"].setBinding(scene));
                REQUIRE_CALL(cursor["results"].setBinding(results));
            }
            pass->dispatchCompute(1, 1, 1);
            pass->end();
        }
        else
        {
            auto pass = encoder->beginRayTracingPass();
            if (root)
                pass->bindPipeline(pipeline.raytracingPipeline, pipeline.shaderTable, root);
            else
            {
                root = pass->bindPipeline(pipeline.raytracingPipeline, pipeline.shaderTable);
                ShaderCursor cursor(root);
                REQUIRE_CALL(cursor["scene"].setBinding(scene));
                REQUIRE_CALL(cursor["results"].setBinding(results));
            }
            pass->dispatchRays(0, 2, 1, 1);
            pass->end();
        }
    };
    trace();
    if (betweenTraces)
    {
        betweenTraces(encoder);
        trace(); // Reuse the same shader bindings after an indirect BLAS write.
    }
    REQUIRE_CALL(queue->submit(encoder->finish()));
    REQUIRE_CALL(queue->waitOnHost());
    compareComputeResult(device, results, makeArray<float>(firstDistance, secondDistance));
}

} // namespace

GPU_TEST_CASE("acceleration-structure-buffer-placement-dependencies", D3D12 | Vulkan)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("Ray tracing not supported");
    auto queue = device->getQueue(QueueType::Graphics);
    TriangleBuild build(device, 0.f, 2.f);
    // Both rays hit this geometry, so identical clones preserve TLAS bounds.
    const Vertex vertices[] = {{0.f, 0.f, 2.f}, {4.f, 0.f, 2.f}, {0.f, 4.f, 2.f}};
    build.vertices = device->createBuffer(build.vertices->getDesc(), vertices);
    REQUIRE(build.vertices);
    build.input.triangles.vertexBuffers[0] = build.vertices;
    auto alignment = device->getInfo().limits.accelerationStructureAlignment;
    Size stride = (build.sizes.accelerationStructureSize + alignment - 1) / alignment * alignment;
    auto scratch = makeStorage(device, build.sizes.scratchSize, BufferUsage::UnorderedAccess);
    for (bool placed : {false, true})
    {
        CAPTURE(placed);
        ComPtr<IBuffer> backing;
        if (placed)
            backing = makeStorage(device, 2 * stride, BufferUsage::AccelerationStructure);
        auto first = makeAS(device, build.sizes.accelerationStructureSize, backing);
        auto second = makeAS(device, build.sizes.accelerationStructureSize, backing, placed ? stride : 0);
        // Batch independent builds, with no shared scratch region.
        auto secondScratch = makeStorage(device, build.sizes.scratchSize, BufferUsage::UnorderedAccess);
        tracePair(
            device,
            queue,
            first,
            second,
            2.f,
            2.f,
            [&](ICommandEncoder* encoder)
            {
                AccelerationStructureBuildEntry entries[] = {
                    {build.desc, first, nullptr, scratch},
                    {build.desc, second, nullptr, secondScratch},
                };
                encoder->buildAccelerationStructures(0, nullptr);
                encoder->buildAccelerationStructures(2, entries);
                // Recording must copy the caller-owned entry array.
                entries[0] = {};
                entries[1] = {};
            }
        );
        for (bool rayQuery : {false, true})
        {
            if (rayQuery && !device->hasFeature(Feature::RayQuery))
                continue;
            CAPTURE(rayQuery);
            // Build -> copy -> TLAS -> traversal in one command buffer. Then
            // overwrite a referenced BLAS with an identical clone and traverse
            // again without rebuilding the TLAS or changing shader bindings.
            tracePair(
                device,
                queue,
                first,
                second,
                2.f,
                2.f,
                [&](ICommandEncoder* encoder)
                {
                    encoder->buildAccelerationStructure(build.desc, first, nullptr, scratch, 0, nullptr);
                    encoder->copyAccelerationStructure(second, first, AccelerationStructureCopyMode::Clone);
                },
                [&](ICommandEncoder* encoder)
                {
                    // Conservative AS boundaries must order this overwrite after traversal,
                    // including indirect BLAS reads, without an explicit barrier.
                    encoder->copyAccelerationStructure(first, second, AccelerationStructureCopyMode::Clone);
                },
                rayQuery
            );
        }
    }
}

GPU_TEST_CASE("acceleration-structure-batch-buffer-roles", D3D12 | Vulkan)
{
    if (!device->hasFeature(Feature::RayTracing))
        SKIP("Ray tracing not supported");
    auto queue = device->getQueue(QueueType::Graphics);
    TriangleBuild firstBuild(device, 0.f, 2.f);
    TriangleBuild secondBuild(device, 2.f, 3.f);
    auto first = makeAS(device, firstBuild.sizes.accelerationStructureSize);
    auto second = makeAS(device, secondBuild.sizes.accelerationStructureSize);

    // One buffer is scratch for the first build and geometry input for the second.
    // The regions are disjoint, but D3D12 must change its whole-resource state
    // between builds rather than applying both states before the batch starts.
    const Vertex vertices[] = {{2.f, 0.f, 3.f}, {3.f, 0.f, 3.f}, {2.f, 1.f, 3.f}};
    Size alignment = device->getInfo().limits.accelerationStructureScratchAlignment;
    Size scratchOffset = (sizeof(vertices) + alignment - 1) / alignment * alignment;
    BufferDesc sharedDesc = {};
    sharedDesc.size = scratchOffset + firstBuild.sizes.scratchSize;
    sharedDesc.usage = BufferUsage::UnorderedAccess | BufferUsage::AccelerationStructureBuildInput;
    sharedDesc.defaultState = ResourceState::UnorderedAccess;
    std::vector<uint8_t> data(sharedDesc.size);
    std::memcpy(data.data(), vertices, sizeof(vertices));
    auto shared = device->createBuffer(sharedDesc, data.data());
    REQUIRE(shared);
    secondBuild.input.triangles.vertexBuffers[0] = shared;
    auto scratch = makeStorage(device, secondBuild.sizes.scratchSize, BufferUsage::UnorderedAccess);

    for (bool reverse : {false, true})
    {
        CAPTURE(reverse);
        tracePair(
            device,
            queue,
            first,
            second,
            2.f,
            3.f,
            [&](ICommandEncoder* encoder)
            {
                AccelerationStructureBuildEntry entries[] = {
                    {firstBuild.desc, first, nullptr, BufferOffsetPair(shared, scratchOffset)},
                    {secondBuild.desc, second, nullptr, scratch},
                };
                if (reverse)
                    std::swap(entries[0], entries[1]);
                encoder->buildAccelerationStructures(2, entries);
            }
        );
    }
}

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
    queryDesc.count = 2;
    ComPtr<IQueryPool> query;
    REQUIRE_CALL(device->createQueryPool(queryDesc, query.writeRef()));
    REQUIRE(query);
    AccelerationStructureQueryDesc property = {};
    property.queryPool = query;
    property.queryType = queryDesc.type;
    auto encoder = queue->createCommandEncoder();
    auto secondScratch = makeStorage(
        device,
        secondBuild.sizes.scratchSize + limits.accelerationStructureScratchAlignment,
        BufferUsage::UnorderedAccess
    );
    {
        auto input = firstBuild.input;
        auto desc = firstBuild.desc;
        desc.inputs = &input;
        AccelerationStructureQueryDesc properties[] = {property, property};
        properties[1].firstQueryIndex = 1;
        AccelerationStructureBuildEntry entries[] = {
            {desc, first, nullptr, scratchRegion, 1, &properties[0]},
            {secondBuild.desc,
             second,
             nullptr,
             BufferOffsetPair(secondScratch, limits.accelerationStructureScratchAlignment),
             1,
             &properties[1]},
        };
        encoder->buildAccelerationStructures(2, entries);
        // Nested input/query descriptors are snapshots, not borrowed caller memory.
        input = {};
        properties[0] = properties[1] = {};
        entries[0] = entries[1] = {};
    }
    secondScratch.setNull(); // The recorded batch retains scratch too.
    // A following single build can reuse the first batch's scratch region.
    encoder->buildAccelerationStructure(firstBuild.desc, automatic, nullptr, scratchRegion, 0, nullptr);
    REQUIRE_CALL(queue->submit(encoder->finish()));
    REQUIRE_CALL(queue->waitOnHost());
    tracePair(device, queue, first, second, 2.f, 3.f);
    tracePair(device, queue, automatic, second, 2.f, 3.f);

    uint64_t compactedSizes[2] = {};
    REQUIRE_CALL(query->getResult(0, 2, compactedSizes));
    REQUIRE_GT(compactedSizes[1], 0);
    uint64_t compactedSize = compactedSizes[0];
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

GPU_TEST_CASE("acceleration-structure-batch-validation", D3D12 | Vulkan | CUDA | DontCreateDevice)
{
    PlacementDiagnostics diagnostics;
    DeviceExtraOptions options;
    options.debugCallback = &diagnostics;
    options.enableValidation = true;
    device = createTestingDevice(ctx, ctx->deviceType, false, &options);
    if (!device || !device->hasFeature(Feature::AccelerationStructure))
        SKIP("Acceleration structures not supported");
    TriangleBuild geometry(device, 0.f, 2.f);
    const auto& limits = device->getInfo().limits;
    Size asStride = (geometry.sizes.accelerationStructureSize + limits.accelerationStructureAlignment - 1) /
                    limits.accelerationStructureAlignment * limits.accelerationStructureAlignment;
    Size scratchSize = std::max(geometry.sizes.scratchSize, geometry.sizes.updateScratchSize);
    Size scratchStride = (scratchSize + limits.accelerationStructureScratchAlignment - 1) /
                         limits.accelerationStructureScratchAlignment * limits.accelerationStructureScratchAlignment;
    auto storage = makeStorage(device, 2 * asStride, BufferUsage::AccelerationStructure);
    auto scratch = makeStorage(device, 2 * scratchStride, BufferUsage::UnorderedAccess);
    auto first = makeAS(device, geometry.sizes.accelerationStructureSize, storage);
    auto second = makeAS(device, geometry.sizes.accelerationStructureSize, storage, asStride);
    auto overlap = makeAS(device, geometry.sizes.accelerationStructureSize, storage);
    AccelerationStructureBuildEntry valid[] = {
        {geometry.desc, first, nullptr, scratch},
        {geometry.desc, second, nullptr, BufferOffsetPair(scratch, scratchStride)},
    };
    auto queue = device->getQueue(QueueType::Graphics);
    auto encoder = queue->createCommandEncoder();
    auto reject = [&](const AccelerationStructureBuildEntry* entries)
    {
        uint32_t before = diagnostics.errors;
        encoder->buildAccelerationStructures(2, entries);
        CHECK_GT(diagnostics.errors, before);
    };
    uint32_t before = diagnostics.errors;
    encoder->buildAccelerationStructures(0, nullptr);
    CHECK_EQ(diagnostics.errors, before);
    reject(nullptr);
    auto invalid = makeArray<AccelerationStructureBuildEntry>(valid[0], valid[1]);
    invalid[1].dst = nullptr;
    reject(invalid.data());
    invalid[1] = valid[1];
    invalid[1].dst = first;
    reject(invalid.data());
    invalid[1].dst = overlap; // Different AS objects, same storage bytes.
    reject(invalid.data());
    invalid[1] = valid[1];
    invalid[1].scratchBuffer = scratch;
    reject(invalid.data());
    invalid[1].scratchBuffer = BufferOffsetPair(scratch, scratch->getDesc().size);
    reject(invalid.data());
    invalid[1] = valid[1];
    invalid[1].desc.mode = AccelerationStructureBuildMode::Update;
    invalid[1].src = first; // A batch entry cannot consume another entry's output.
    reject(invalid.data());

    QueryPoolDesc queryDesc = {};
    queryDesc.type = QueryType::AccelerationStructureCompactedSize;
    queryDesc.count = 1;
    ComPtr<IQueryPool> query;
    REQUIRE_CALL(device->createQueryPool(queryDesc, query.writeRef()));
    REQUIRE(query);
    AccelerationStructureQueryDesc property = {queryDesc.type, query, 0};
    invalid[0] = valid[0];
    invalid[1] = valid[1];
    for (auto& entry : invalid)
    {
        entry.propertyQueryCount = 1;
        entry.queryDescs = &property;
    }
    reject(invalid.data());

    before = diagnostics.errors;
    encoder->buildAccelerationStructures(2, valid);
    // A subsequent batch may reuse the very same storage and scratch regions.
    for (auto& entry : valid)
    {
        entry.desc.mode = AccelerationStructureBuildMode::Update;
        entry.src = entry.dst;
    }
    encoder->buildAccelerationStructures(2, valid);
    REQUIRE_CALL(queue->submit(encoder->finish()));
    REQUIRE_CALL(queue->waitOnHost());
    CHECK_EQ(diagnostics.errors, before);
}

GPU_TEST_CASE("acceleration-structure-buffer-placement-validation", D3D12 | Vulkan | CUDA | Metal | DontCreateDevice)
{
    PlacementDiagnostics diagnostics;
    DeviceExtraOptions options;
    options.debugCallback = &diagnostics;
    device = createTestingDevice(ctx, ctx->deviceType, false, &options);
    if (!device || !device->hasFeature(Feature::AccelerationStructure))
        SKIP("Acceleration structures not supported");
    CHECK_GT(device->getInfo().limits.accelerationStructureScratchAlignment, 0);
    auto backing = makeStorage(device, 4096, BufferUsage::AccelerationStructure);
    Device* innerDevice = checked_cast<Buffer*>(backing.get())->getDevice();
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
        // The backend must still reject invalid storage without the RHI validation
        // layer, but diagnostics belong exclusively to that layer.
        before = diagnostics.errors;
        CHECK(SLANG_FAILED(innerDevice->createAccelerationStructure(desc, structure.writeRef())));
        CHECK_FALSE(structure);
        CHECK_EQ(diagnostics.errors, before);
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
    Size scratchAlignment = limits.accelerationStructureScratchAlignment;
    Size scratchStride = (scratchSize + scratchAlignment - 1) / scratchAlignment * scratchAlignment;
    auto scratch = makeStorage(device, scratchAlignment + kDynamicCount * scratchStride, BufferUsage::UnorderedAccess);
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
    std::array<AccelerationStructureBuildEntry, kStaticCount> staticEntries;
    for (uint32_t mesh = 0; mesh < kStaticCount; ++mesh)
        staticEntries[mesh] = {
            staticBuilds[mesh]->desc,
            staticBlas[mesh],
            nullptr,
            BufferOffsetPair(scratch, scratchAlignment + mesh * scratchStride)
        };
    encoder->buildAccelerationStructures(kStaticCount, staticEntries.data());
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
        std::array<AccelerationStructureBuildEntry, kDynamicCount> entries;
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
            entries[mesh] = {
                build.desc,
                structure,
                refit ? structure.get() : nullptr,
                BufferOffsetPair(scratch, scratchAlignment + mesh * scratchStride)
            };
            // The ray hits at barycentrics (0.25, 0.25), so interpolate vertex Z analytically.
            expected[current][kStaticCount + mesh] = z + 0.75f * slope;
        }
        encoder->buildAccelerationStructures(kDynamicCount, entries.data());
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
