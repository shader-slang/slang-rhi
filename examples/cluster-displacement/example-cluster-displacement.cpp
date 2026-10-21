// Filtered procedural terrain with GPU-selected subdivision and cluster ray tracing.
// Requires surface, acceleration-structure, cluster-acceleration-structure, and
// ray-tracing support on D3D12, Vulkan, or CUDA/OptiX.
//
// Build: cmake --build build --config Debug --target example-cluster-displacement
// Run:   build/Debug/example-cluster-displacement --device d3d12
// Use --device vulkan or --device cuda for the other supported backends.
//
// A persistent sparse quadtree covers a 12-by-12 quad at levels 0-12, with an
// 8,192-patch budget independent of depth. Two adaptation rounds per frame merge
// leaf siblings, propagate refinement requests to coarse neighbors, then split
// in projected-edge priority buckets. A cooperative GPU group allocates/recycles
// sibling blocks in parallel. Every intermediate tree covers the surface
// and has at most a one-level difference across edges. Teleports settle over
// several frames. Budget pressure gradually raises the target size to reclaim
// less useful patches; hysteresis reduces split/merge churn.
// Sixteen transition patterns omit unmatched fine-edge vertices. Canonical
// integer coordinates define both positions and filter footprints, including at
// corners. Leaf filter footprints extend smoothly across two coarse mesh cells;
// sparse tree queries evaluate their maximum without a dense filter lattice.
// This avoids narrow ridges when adjacent patches use different noise bands.
// Thirteen filtered fBm octaves produce a smooth basin and rocky hills, with a
// localized moving bulge. Each patch has 81 vertices and at most 128 triangles.
// The uniform reference stays capped at level 6 to fit the same buffers.
// There is no LOD morphing; changing the filter footprint can still cause popping.
//
// Each frame selects and balances the tree, compacts leaves, generates geometry,
// builds CLAS from triangles and a BLAS from the CLAS handles, patches the native
// TLAS instance on the GPU, rebuilds the TLAS, and traces primary and shadow rays.
// Selection and AS handles stay on the GPU; only overlay statistics are read back.
// Build submission precedes trace recording because CUDA obtains the TLAS handle
// during submission. One resource set is reused after a queue wait; the overlay reports requested geometry/AS bytes,
// excluding the render texture, swapchain, pipelines, and driver overhead.
//
// Controls: left drag to look; W/A/S/D and Q/E to move; Shift to move faster.
// Space pauses animation; F freezes LOD; T toggles adaptive/uniform selection.
// The selection modes retain independent state, including while frozen.
// Minus/plus adjusts the target projected mesh edge (64-0.5 pixels); smaller is
// finer. Adaptive selection measures displaced triangle edges, including stitches.
// The split threshold allows 15% hysteresis; depth and patch-budget limits can
// prevent reaching the requested density. The overlay shows budget relaxation.
// The uniform reference retains its simpler distance-based density estimate.
// Brackets adjust amplitude (0-3). Keys 1-4 select shaded, cluster, level, or solid
// triangle colors; C cycles them. Triangle colors are stable for unchanged topology.
// X approaches the surface at the center of the view by 5x (and slows movement).
// Z moves to a detail view; R returns to the overview. Scroll changes movement
// speed for close exploration (0.0001-30 units/second); Shift temporarily speeds up.
// F1 toggles the overlay; F2 fits windows; F3 toggles VSync; F4 toggles profiling.
// GPU timings separate LOD evaluation/balancing/allocation, geometry generation,
// CLAS/BLAS/TLAS builds, and ray tracing. Repeated LOD rounds are summed; timing
// excludes presentation and CPU recording. Escape closes.

#include "../base/example.h"
#include "../base/camera.h"

#include <slang-rhi/acceleration-structure-utils.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

using namespace rhi;

// Passed to the Slang session as preprocessor macros so host and shader bounds agree.
constexpr uint32_t kMaxLevel = 12;
constexpr uint32_t kPatchBudget = 8192;
constexpr uint32_t kUniformMaxLevel = 6;
constexpr float kSurfaceSize = 12.f;
constexpr float kTargetEdgePixels = 8.f;
constexpr float kLodHysteresis = 0.15f;

// Bits: -X, +X, -Z, +Z. Each pattern occupies 128 triangles; only its
// active prefix is used. Stitching removes four triangles per marked edge.
std::array<uint32_t, 16 * 128 * 3> makeTransitionPatterns()
{
    std::array<uint32_t, 16 * 128 * 3> indices = {};
    for (uint32_t mask = 0; mask < 16; ++mask)
    {
        uint32_t offset = mask * 128 * 3;
        // Keep the original row-major triangle order in the uniform reference.
        if (mask == 0)
        {
            for (uint32_t y = 0; y < 8; ++y)
                for (uint32_t x = 0; x < 8; ++x)
                {
                    uint32_t v = y * 9 + x;
                    for (auto index : {v, v + 9, v + 1, v + 1, v + 9, v + 10})
                        indices[offset++] = index;
                }
            continue;
        }
        // A fan in each 2x2 cell block handles two stitched edges at a corner
        // together. Omitting odd boundary vertices leaves exactly the coarse
        // neighbor's segments, with no collapsed triangles or internal T-junctions.
        for (uint32_t y = 0; y < 8; y += 2)
            for (uint32_t x = 0; x < 8; x += 2)
            {
                uint32_t v = y * 9 + x;
                uint32_t ring[8] = {v, v + 9, v + 18, v + 19, v + 20, v + 11, v + 2, v + 1};
                uint32_t polygon[8], count = 0;
                for (uint32_t i = 0; i < 8; ++i)
                {
                    if ((i == 1 && x == 0 && (mask & 1)) || (i == 5 && x == 6 && (mask & 2)) ||
                        (i == 7 && y == 0 && (mask & 4)) || (i == 3 && y == 6 && (mask & 8)))
                        continue;
                    polygon[count++] = ring[i];
                }
                for (uint32_t i = 0; i < count; ++i)
                {
                    indices[offset++] = v + 10;
                    indices[offset++] = polygon[i];
                    indices[offset++] = polygon[(i + 1) % count];
                }
            }
    }
    return indices;
}

struct LodSettings
{
    math::float3 cameraPosition = {10, 7, 11};
    // Vertical focal length in pixels: viewport height / (2 * tan(vertical FOV / 2)).
    float projectionScale = 1;
    math::float3 cameraRight = {1, 0, 0};
    math::float3 cameraUp = {0, 0, -1};
    math::float3 cameraForward = {0, -1, 0};
    // Tangents of the horizontal/vertical half FOV.
    math::float2 viewHalfExtent = {1, 1};
    float quality = 1;
    bool frozen = false;
    bool adaptive = true;
};

// A balanced quadtree (or uniform reference) covers the whole quad with 8 x 8 cell patches.
struct Surface
{
    static constexpr uint32_t kPatchCount = kPatchBudget;
    static constexpr uint32_t kBlockCount = (kPatchCount - 1) / 3;
    static constexpr uint32_t kNodeCount = 1 + 4 * kBlockCount;
    static_assert((1u << (2 * kUniformMaxLevel)) <= kPatchCount);
    static_assert(kMaxLevel >= kUniformMaxLevel && kMaxLevel <= 16);
    static constexpr uint32_t kVertexCount = 81;
    static constexpr uint32_t kTriangleCount = 128;

    ComPtr<IBuffer> vertices, indices, patches, triangleArgs, clusterArgs, counts, lodState, tree;
    ComPtr<IBuffer> requests, mergeRequests, freeBlocks, allocator;
    ComPtr<IBuffer> clasData, clasHandles, clasScratch, blasData, blasHandles, blasScratch;
    ComPtr<IBuffer> instances, tlasScratch;
    ComPtr<IAccelerationStructure> tlas;
    ComPtr<IComputePipeline> selectPipeline, generatePipeline, instancePipeline;
    ComPtr<IComputePipeline> treePipeline, balancePipeline, allocatePipeline, compactPipeline, finishPipeline;
    ClusterOperationDesc clasBuild = {}, blasBuild = {};
    AccelerationStructureBuildInput tlasInput = {};
    uint32_t handleOffset = 0;
    uint64_t allocatedBytes = 0;

    Result buffer(
        IDevice* device,
        uint64_t size,
        BufferUsage usage,
        const char* label,
        ComPtr<IBuffer>& result,
        const void* data = nullptr
    )
    {
        BufferDesc desc = {};
        desc.size = size;
        desc.usage = usage | BufferUsage::CopySource;
        desc.label = label;
        SLANG_RETURN_ON_FAIL(device->createBuffer(desc, data, result.writeRef()));
        allocatedBytes += size;
        return SLANG_OK;
    }

    Result initialize(IDevice* device, const char* shaderModule)
    {
        // These backends have a 64-bit BLAS address or OptiX traversable handle in their native instance record.
        // Derive the byte offset from the public native layouts, not a shader constant.
        switch (device->getDeviceType())
        {
        case DeviceType::D3D12:
            handleOffset = uint32_t(offsetof(AccelerationStructureInstanceDescD3D12, AccelerationStructure));
            break;
        case DeviceType::Vulkan:
            handleOffset = uint32_t(offsetof(AccelerationStructureInstanceDescVulkan, accelerationStructureReference));
            break;
        case DeviceType::CUDA:
            handleOffset = uint32_t(offsetof(AccelerationStructureInstanceDescOptix, traversableHandle));
            break;
        default:
            return SLANG_E_NOT_AVAILABLE;
        }
        SLANG_RETURN_ON_FAIL(createComputePipeline(device, shaderModule, "selectLevel", selectPipeline.writeRef()));
        SLANG_RETURN_ON_FAIL(
            createComputePipeline(device, shaderModule, "generateSurface", generatePipeline.writeRef())
        );
        SLANG_RETURN_ON_FAIL(createComputePipeline(device, shaderModule, "writeInstance", instancePipeline.writeRef()));

        SLANG_RETURN_ON_FAIL(createComputePipeline(device, shaderModule, "selectTree", treePipeline.writeRef()));
        SLANG_RETURN_ON_FAIL(createComputePipeline(device, shaderModule, "balanceTree", balancePipeline.writeRef()));
        SLANG_RETURN_ON_FAIL(createComputePipeline(device, shaderModule, "compactLeaves", compactPipeline.writeRef()));
        SLANG_RETURN_ON_FAIL(createComputePipeline(device, shaderModule, "finishSelection", finishPipeline.writeRef()));
        SLANG_RETURN_ON_FAIL(createComputePipeline(device, shaderModule, "allocateTree", allocatePipeline.writeRef()));

        const auto input =
            BufferUsage::ShaderResource | BufferUsage::UnorderedAccess | BufferUsage::AccelerationStructureBuildInput;
        SLANG_RETURN_ON_FAIL(buffer(device, kPatchCount * kVertexCount * 12, input, "vertices", vertices));
        SLANG_RETURN_ON_FAIL(buffer(device, kPatchCount * 4 * sizeof(uint32_t), input, "patches", patches));
        auto indexData = makeTransitionPatterns();
        SLANG_RETURN_ON_FAIL(
            buffer(device, indexData.size() * sizeof(uint32_t), input, "indices", indices, indexData.data())
        );
        SLANG_RETURN_ON_FAIL(
            buffer(device, kPatchCount * sizeof(TriangleClusterArgs), input, "triangleArgs", triangleArgs)
        );
        SLANG_RETURN_ON_FAIL(buffer(device, sizeof(ClusterArgs), input, "clusterArgs", clusterArgs));
        SLANG_RETURN_ON_FAIL(buffer(device, 3 * sizeof(uint32_t), input, "counts", counts));
        const uint32_t initialState[] = {0, 0}; // uniform level, uniform initialized
        SLANG_RETURN_ON_FAIL(buffer(device, sizeof(initialState), input, "lodState", lodState, initialState));

        // Node: patch x/y, level, first child (0 = leaf, ~0u = unused).
        // Four siblings always occupy one block; only complete quartets are recycled.
        std::vector<std::array<uint32_t, 4>> initialTree(kNodeCount, {0, 0, 0, ~0u});
        initialTree[0] = {0, 0, 0, 0};
        std::vector<uint32_t> initialBlocks(kBlockCount);
        for (uint32_t i = 0; i < kBlockCount; ++i)
            initialBlocks[i] = 1 + 4 * i;
        const uint32_t initialAllocator[] = {kBlockCount, 0x3f800000}; // free blocks, float target multiplier (1)
        SLANG_RETURN_ON_FAIL(buffer(device, kNodeCount * 16, input, "tree", tree, initialTree.data()));
        std::vector<uint32_t> initialRequests(kNodeCount * 2, 0);
        SLANG_RETURN_ON_FAIL(buffer(device, kNodeCount * 8, input, "requests", requests, initialRequests.data()));
        SLANG_RETURN_ON_FAIL(buffer(device, kNodeCount * 8, input, "mergeRequests", mergeRequests));
        SLANG_RETURN_ON_FAIL(buffer(device, kBlockCount * 4, input, "freeBlocks", freeBlocks, initialBlocks.data()));
        SLANG_RETURN_ON_FAIL(buffer(device, sizeof(initialAllocator), input, "allocator", allocator, initialAllocator));

        clasBuild.params.type = ClusterOperationType::CLASFromTriangles;
        clasBuild.params.mode = ClusterOperationMode::ImplicitDestinations;
        clasBuild.params.maxArgCount = kPatchCount;
        auto& clas = clasBuild.params.clas;
        clas.maxTriangleCount = kTriangleCount;
        clas.maxVertexCount = kVertexCount;
        clas.maxTotalTriangleCount = kPatchCount * kTriangleCount;
        clas.maxTotalVertexCount = kPatchCount * kVertexCount;
        clasBuild.argsBuffer = triangleArgs;
        clasBuild.argsBufferStride = sizeof(TriangleClusterArgs);
        clasBuild.argCountBuffer = counts;

        blasBuild.params.type = ClusterOperationType::BLASFromCLAS;
        blasBuild.params.mode = ClusterOperationMode::ImplicitDestinations;
        blasBuild.params.maxArgCount = 1;
        blasBuild.params.blas.maxClasCount = kPatchCount;
        blasBuild.params.blas.maxTotalClasCount = kPatchCount;
        blasBuild.argsBuffer = clusterArgs;
        blasBuild.argsBufferStride = sizeof(ClusterArgs);
        blasBuild.argCountBuffer = BufferOffsetPair(counts, sizeof(uint32_t));

        auto allocateBuild = [&](ClusterOperationDesc& build,
                                 ComPtr<IBuffer>& data,
                                 ComPtr<IBuffer>& handles,
                                 ComPtr<IBuffer>& scratch) -> Result
        {
            ClusterOperationSizes sizes = {};
            SLANG_RETURN_ON_FAIL(device->getClusterOperationSizes(build.params, &sizes));
            // Dedicated buffers start at offset zero; device allocations supply native alignment.
            uint64_t handleBytes = (uint64_t(build.params.maxArgCount) * 8 + kClusterOutputAlignment - 1) &
                                   ~uint64_t(kClusterOutputAlignment - 1);
            SLANG_RETURN_ON_FAIL(
                buffer(device, sizes.resultSize, BufferUsage::AccelerationStructure, "clusterData", data)
            );
            // Vulkan requires AS-storage usage for the destination address array. On D3D12
            // this is an ordinary UAV, not an AS resource with a permanent native AS state.
            auto handleUsage = input;
            if (device->getDeviceType() == DeviceType::Vulkan)
                handleUsage |= BufferUsage::AccelerationStructure;
            SLANG_RETURN_ON_FAIL(buffer(device, handleBytes, handleUsage, "clusterHandles", handles));
            SLANG_RETURN_ON_FAIL(
                buffer(device, sizes.scratchSize, BufferUsage::UnorderedAccess, "clusterScratch", scratch)
            );
            build.resultBuffer = data;
            build.addressesBuffer = handles;
            build.scratchBuffer = scratch;
            return SLANG_OK;
        };
        SLANG_RETURN_ON_FAIL(allocateBuild(clasBuild, clasData, clasHandles, clasScratch));
        SLANG_RETURN_ON_FAIL(allocateBuild(blasBuild, blasData, blasHandles, blasScratch));

        AccelerationStructureInstanceDescGeneric instance = {};
        instance.transform[0][0] = instance.transform[1][1] = instance.transform[2][2] = 1;
        instance.instanceMask = 0xff;
        auto type = getAccelerationStructureInstanceDescType(device);
        uint32_t stride = uint32_t(getAccelerationStructureInstanceDescSize(type));
        std::vector<uint8_t> native(stride);
        convertAccelerationStructureInstanceDescs(1, type, native.data(), stride, &instance, sizeof(instance));
        SLANG_RETURN_ON_FAIL(buffer(device, stride, input, "tlasInstance", instances, native.data()));
        tlasInput.type = AccelerationStructureBuildInputType::Instances;
        tlasInput.instances.instanceBuffer = instances;
        tlasInput.instances.instanceCount = 1;
        tlasInput.instances.instanceStride = stride;
        AccelerationStructureBuildDesc build = {};
        build.inputs = &tlasInput;
        build.inputCount = 1;
        AccelerationStructureSizes sizes = {};
        SLANG_RETURN_ON_FAIL(device->getAccelerationStructureSizes(build, &sizes));
        SLANG_RETURN_ON_FAIL(
            buffer(device, sizes.scratchSize, BufferUsage::UnorderedAccess, "tlasScratch", tlasScratch)
        );
        AccelerationStructureDesc desc = {};
        desc.kind = AccelerationStructureKind::TopLevel;
        desc.size = sizes.accelerationStructureSize;
        desc.label = "surfaceTLAS";
        allocatedBytes += desc.size;
        return device->createAccelerationStructure(desc, tlas.writeRef());
    }

    Result rebuild(ICommandQueue* queue, float time, float amplitude, const LodSettings& lod)
    {
        // CUDA obtains the TLAS traversable handle when the build is submitted. Shader
        // bindings capture that handle during recording, so submit builds before recording
        // tracing. Both submissions use the same queue; no intermediate wait or readback.
        ComPtr<ICommandEncoder> encoder;
        SLANG_RETURN_ON_FAIL(queue->createCommandEncoder(encoder.writeRef()));
        SLANG_RETURN_ON_FAIL(build(encoder, time, amplitude, lod));
        ComPtr<ICommandBuffer> commands;
        SLANG_RETURN_ON_FAIL(encoder->finish(commands.writeRef()));
        return queue->submit(commands);
    }

    Result build(ICommandEncoder* encoder, float time, float amplitude, const LodSettings& lod)
    {
        if (lod.projectionScale <= 0 || lod.quality <= 0)
            return SLANG_E_INVALID_ARG;
        Profiler::pushRegion(encoder, "Surface rebuild");
        Profiler::pushRegion(encoder, "LOD selection");
        auto pass = encoder->beginComputePass();
        ShaderCursor cursor(pass->bindPipeline(selectPipeline)->getEntryPoint(0));
        SLANG_RETURN_ON_FAIL(cursor["adaptive"].setData(lod.adaptive ? 1u : 0u));
        SLANG_RETURN_ON_FAIL(cursor["lodState"].setBinding(lodState));
        SLANG_RETURN_ON_FAIL(cursor["counts"].setBinding(counts));
        SLANG_RETURN_ON_FAIL(cursor["clusterArgs"].setBinding(clusterArgs));
        SLANG_RETURN_ON_FAIL(cursor["handlesAddress"].setData(clasHandles->getDeviceAddress()));
        SLANG_RETURN_ON_FAIL(cursor["cameraPosition"].setData(lod.cameraPosition));
        SLANG_RETURN_ON_FAIL(cursor["projectionScale"].setData(lod.projectionScale));
        SLANG_RETURN_ON_FAIL(cursor["quality"].setData(lod.quality));
        SLANG_RETURN_ON_FAIL(cursor["frozen"].setData(lod.frozen ? 1u : 0u));
        SLANG_RETURN_ON_FAIL(cursor["amplitude"].setData(amplitude));
        pass->dispatchCompute(1, 1, 1);
        pass->end();
        encoder->globalBarrier();

        if (lod.adaptive)
        {
            // Separate immutable neighbor queries from mutations. This keeps simultaneous
            // merges/splits balanced without atomically editing a tree being traversed.
            for (uint32_t round = 0; round < 2 && !lod.frozen; ++round)
                for (uint32_t splitting = 0; splitting < 2; ++splitting)
                {
                    Profiler::pushRegion(encoder, splitting ? "Evaluate splits" : "Evaluate merges");
                    pass = encoder->beginComputePass();
                    cursor = ShaderCursor(pass->bindPipeline(treePipeline)->getEntryPoint(0));
                    SLANG_RETURN_ON_FAIL(cursor["tree"].setBinding(tree));
                    SLANG_RETURN_ON_FAIL(cursor["requests"].setBinding(splitting ? requests : mergeRequests));
                    SLANG_RETURN_ON_FAIL(cursor["previousSplits"].setBinding(splitting ? mergeRequests : requests));
                    SLANG_RETURN_ON_FAIL(cursor["indices"].setBinding(indices));
                    SLANG_RETURN_ON_FAIL(cursor["allocator"].setBinding(allocator));
                    SLANG_RETURN_ON_FAIL(cursor["splitting"].setData(splitting));
                    SLANG_RETURN_ON_FAIL(cursor["cameraPosition"].setData(lod.cameraPosition));
                    SLANG_RETURN_ON_FAIL(cursor["cameraRight"].setData(lod.cameraRight));
                    SLANG_RETURN_ON_FAIL(cursor["cameraUp"].setData(lod.cameraUp));
                    SLANG_RETURN_ON_FAIL(cursor["cameraForward"].setData(lod.cameraForward));
                    SLANG_RETURN_ON_FAIL(cursor["viewHalfExtent"].setData(lod.viewHalfExtent));
                    SLANG_RETURN_ON_FAIL(cursor["projectionScale"].setData(lod.projectionScale));
                    SLANG_RETURN_ON_FAIL(cursor["quality"].setData(lod.quality));
                    SLANG_RETURN_ON_FAIL(cursor["amplitude"].setData(amplitude));
                    SLANG_RETURN_ON_FAIL(cursor["time"].setData(time));
                    pass->dispatchCompute(kNodeCount, 1, 1);
                    pass->end();
                    encoder->globalBarrier();
                    Profiler::popRegion(encoder);
                    if (splitting)
                    {
                        Profiler::pushRegion(encoder, "Balance tree");
                        for (uint32_t level = kMaxLevel - 1; level > 0; --level)
                        {
                            pass = encoder->beginComputePass();
                            cursor = ShaderCursor(pass->bindPipeline(balancePipeline)->getEntryPoint(0));
                            SLANG_RETURN_ON_FAIL(cursor["tree"].setBinding(tree));
                            SLANG_RETURN_ON_FAIL(cursor["requests"].setBinding(requests));
                            SLANG_RETURN_ON_FAIL(cursor["level"].setData(level));
                            pass->dispatchCompute((kNodeCount + 63) / 64, 1, 1);
                            pass->end();
                            encoder->globalBarrier();
                        }
                        Profiler::popRegion(encoder);
                    }
                    Profiler::pushRegion(encoder, "Allocate / recycle");
                    pass = encoder->beginComputePass();
                    cursor = ShaderCursor(pass->bindPipeline(allocatePipeline)->getEntryPoint(0));
                    SLANG_RETURN_ON_FAIL(cursor["tree"].setBinding(tree));
                    SLANG_RETURN_ON_FAIL(cursor["requests"].setBinding(splitting ? requests : mergeRequests));
                    SLANG_RETURN_ON_FAIL(cursor["freeBlocks"].setBinding(freeBlocks));
                    SLANG_RETURN_ON_FAIL(cursor["allocator"].setBinding(allocator));
                    SLANG_RETURN_ON_FAIL(cursor["splitting"].setData(splitting));
                    pass->dispatchCompute(1, 1, 1);
                    pass->end();
                    encoder->globalBarrier();
                    Profiler::popRegion(encoder);
                }
            Profiler::pushRegion(encoder, "Compact leaves");
            pass = encoder->beginComputePass();
            cursor = ShaderCursor(pass->bindPipeline(compactPipeline)->getEntryPoint(0));
            SLANG_RETURN_ON_FAIL(cursor["tree"].setBinding(tree));
            SLANG_RETURN_ON_FAIL(cursor["counts"].setBinding(counts));
            SLANG_RETURN_ON_FAIL(cursor["patches"].setBinding(patches));
            pass->dispatchCompute((kNodeCount + 63) / 64, 1, 1);
            pass->end();
            encoder->globalBarrier();
            pass = encoder->beginComputePass();
            cursor = ShaderCursor(pass->bindPipeline(finishPipeline)->getEntryPoint(0));
            SLANG_RETURN_ON_FAIL(cursor["counts"].setBinding(counts));
            SLANG_RETURN_ON_FAIL(cursor["clusterArgs"].setBinding(clusterArgs));
            SLANG_RETURN_ON_FAIL(cursor["handlesAddress"].setData(clasHandles->getDeviceAddress()));
            pass->dispatchCompute(1, 1, 1);
            pass->end();
            encoder->globalBarrier();
            Profiler::popRegion(encoder);
        }
        Profiler::popRegion(encoder); // LOD selection

        Profiler::pushRegion(encoder, "Generate geometry");
        pass = encoder->beginComputePass();
        cursor = ShaderCursor(pass->bindPipeline(generatePipeline)->getEntryPoint(0));
        SLANG_RETURN_ON_FAIL(cursor["adaptive"].setData(lod.adaptive ? 1u : 0u));
        SLANG_RETURN_ON_FAIL(cursor["vertices"].setBinding(vertices));
        SLANG_RETURN_ON_FAIL(cursor["patches"].setBinding(patches));
        SLANG_RETURN_ON_FAIL(cursor["triangleArgs"].setBinding(triangleArgs));
        SLANG_RETURN_ON_FAIL(cursor["counts"].setBinding(counts));
        SLANG_RETURN_ON_FAIL(cursor["lodState"].setBinding(lodState));
        SLANG_RETURN_ON_FAIL(cursor["tree"].setBinding(tree));
        SLANG_RETURN_ON_FAIL(cursor["vertexAddress"].setData(vertices->getDeviceAddress()));
        SLANG_RETURN_ON_FAIL(cursor["indexAddress"].setData(indices->getDeviceAddress()));
        SLANG_RETURN_ON_FAIL(cursor["time"].setData(time));
        SLANG_RETURN_ON_FAIL(cursor["amplitude"].setData(amplitude));
        pass->dispatchCompute(kPatchCount, 1, 1);
        pass->end();

        // Address-only inputs are invisible to automatic shader-binding state tracking.
        encoder->globalBarrier();
        encoder->setBufferState(vertices, ResourceState::AccelerationStructureBuildInput);
        encoder->setBufferState(indices, ResourceState::AccelerationStructureBuildInput);
        Profiler::popRegion(encoder); // Generate geometry
        Profiler::pushRegion(encoder, "Build CLAS");
        encoder->executeClusterOperation(clasBuild);
        encoder->globalBarrier();
        encoder->setBufferState(clasData, ResourceState::AccelerationStructureRead);
        encoder->setBufferState(clasHandles, ResourceState::ShaderResource);
        Profiler::popRegion(encoder);
        Profiler::pushRegion(encoder, "Build BLAS");
        encoder->executeClusterOperation(blasBuild);
        encoder->globalBarrier();
        encoder->setBufferState(blasData, ResourceState::AccelerationStructureRead);
        Profiler::popRegion(encoder);

        Profiler::pushRegion(encoder, "Build TLAS");

        pass = encoder->beginComputePass();
        cursor = ShaderCursor(pass->bindPipeline(instancePipeline)->getEntryPoint(0));
        SLANG_RETURN_ON_FAIL(cursor["handles"].setBinding(blasHandles));
        SLANG_RETURN_ON_FAIL(cursor["instances"].setBinding(instances));
        SLANG_RETURN_ON_FAIL(cursor["handleOffset"].setData(handleOffset));
        pass->dispatchCompute(1, 1, 1);
        pass->end();
        encoder->globalBarrier();
        AccelerationStructureBuildDesc build = {};
        build.inputs = &tlasInput;
        build.inputCount = 1;
        encoder->buildAccelerationStructure(build, tlas, nullptr, tlasScratch, 0, nullptr);
        encoder->globalBarrier();
        Profiler::popRegion(encoder); // Build TLAS
        Profiler::popRegion(encoder); // Surface rebuild
        return SLANG_OK;
    }
};

struct Renderer
{
    ComPtr<IRayTracingPipeline> pipeline;
    ComPtr<IShaderTable> table;
    ComPtr<IBuffer> focus;

    Result initialize(IDevice* device, const char* module, const char* raygen = "renderRaygen")
    {
        ComPtr<IShaderProgram> program;
        SLANG_RETURN_ON_FAIL(createProgram(device, module, {raygen, "mainMiss", "mainClosestHit"}, program.writeRef()));
        HitGroupDesc group = {};
        group.hitGroupName = "surface";
        group.closestHitEntryPoint = "mainClosestHit";
        RayTracingPipelineDesc desc = {};
        desc.program = program;
        desc.hitGroupCount = 1;
        desc.hitGroups = &group;
        desc.maxRecursion = 1;       // Primary and shadow rays are both launched from raygen.
        desc.maxRayPayloadSize = 24; // CUDA aligns the trailing float2 barycentrics to eight bytes.
        desc.maxAttributeSizeInBytes = 8;
        desc.flags = RayTracingPipelineFlags::EnableClusters;
        SLANG_RETURN_ON_FAIL(device->createRayTracingPipeline(desc, pipeline.writeRef()));
        const char* miss = "mainMiss";
        const char* hit = "surface";
        ShaderTableDesc tableDesc = {};
        tableDesc.program = program;
        tableDesc.rayGenShaderCount = 1;
        tableDesc.rayGenShaderEntryPointNames = &raygen;
        tableDesc.missShaderCount = 1;
        tableDesc.missShaderEntryPointNames = &miss;
        tableDesc.hitGroupCount = 1;
        tableDesc.hitGroupNames = &hit;
        SLANG_RETURN_ON_FAIL(device->createShaderTable(tableDesc, table.writeRef()));
        BufferDesc focusDesc = {};
        focusDesc.size = 4 * sizeof(float);
        focusDesc.usage = BufferUsage::UnorderedAccess | BufferUsage::CopySource;
        focusDesc.label = "focusPosition";
        return device->createBuffer(focusDesc, nullptr, focus.writeRef());
    }
};

class ExampleClusterDisplacement : public ExampleBase
{
public:
    Result init(DeviceType deviceType) override
    {
        SLANG_RETURN_ON_FAIL(createDevice(
            deviceType,
            {Feature::Surface,
             Feature::AccelerationStructure,
             Feature::ClusterAccelerationStructure,
             Feature::RayTracing},
            {{"CLUSTER_MAX_LEVEL", std::to_string(kMaxLevel)},
             {"CLUSTER_PATCH_BUDGET", std::to_string(kPatchBudget)},
             {"CLUSTER_UNIFORM_MAX_LEVEL", std::to_string(kUniformMaxLevel)},
             {"CLUSTER_SURFACE_SIZE", std::to_string(kSurfaceSize)},
             {"CLUSTER_TARGET_EDGE_PIXELS", std::to_string(kTargetEdgePixels)},
             {"CLUSTER_LOD_HYSTERESIS", std::to_string(kLodHysteresis)}},
            m_device.writeRef()
        ));
        if (m_device->getDeviceType() != DeviceType::D3D12 && m_device->getDeviceType() != DeviceType::Vulkan &&
            m_device->getDeviceType() != DeviceType::CUDA)
        {
            LOG_INFO("Cluster displacement currently supports D3D12, Vulkan, and CUDA.");
            return SLANG_E_NOT_AVAILABLE;
        }
        SLANG_RETURN_ON_FAIL(createWindow(m_device));
        SLANG_RETURN_ON_FAIL(createSurface(m_device, Format::Undefined, m_surface.writeRef()));
        SLANG_RETURN_ON_FAIL(initOverlay(m_device, m_surface->getConfig()->format));
        SLANG_RETURN_ON_FAIL(initProfiler(m_device));
        SLANG_RETURN_ON_FAIL(m_device->getQueue(QueueType::Graphics, m_queue.writeRef()));
        m_blitter = std::make_unique<Blitter>(m_device);
        SLANG_RETURN_ON_FAIL(m_scene.initialize(m_device, "cluster-displacement.slang"));
        SLANG_RETURN_ON_FAIL(m_renderer.initialize(m_device, "cluster-displacement.slang"));
        BufferDesc statsDesc = {};
        statsDesc.size = 4 * sizeof(uint32_t);
        statsDesc.usage = BufferUsage::CopyDestination;
        statsDesc.memoryType = MemoryType::ReadBack;
        statsDesc.label = "lodStatistics";
        SLANG_RETURN_ON_FAIL(m_device->createBuffer(statsDesc, nullptr, m_lodStatistics.writeRef()));
        m_controller.setCamera(&m_camera);
        resetCamera();
        return SLANG_OK;
    }

    void shutdown() override
    {
        if (m_queue)
            m_queue->waitOnHost();
        m_renderer = {};
        m_scene = {};
        m_output.setNull();
        m_lodStatistics.setNull();
        m_blitter.reset();
        m_queue.setNull();
        m_surface.setNull();
        m_device.setNull();
    }

    Result update(double time) override
    {
        double elapsed = m_previousTime < 0 ? 0 : std::max(0.0, time - m_previousTime);
        m_previousTime = time;
        m_controller.update(float(std::min(elapsed, 0.1)));
        if (!m_paused)
            m_animationTime += float(elapsed);
        return SLANG_OK;
    }

    Result draw() override
    {
        if (!m_surface->getConfig())
            return SLANG_OK;
        // One resource set: retire the preceding frame before reusing geometry, scratch,
        // handles, TLAS, or the output texture. This wait never reads geometry or handles.
        SLANG_RETURN_ON_FAIL(m_queue->waitOnHost());
        // The existing frame wait also retires the small statistics copy. Mapping
        // this persistent readback buffer adds no submission or extra GPU wait.
        uint32_t patchCount = 0, deepestLevel = 0;
        float pressure = 1;
        if (m_haveLodStatistics)
        {
            void* data = nullptr;
            SLANG_RETURN_ON_FAIL(m_device->mapBuffer(m_lodStatistics, CpuAccessMode::Read, &data));
            std::memcpy(&patchCount, data, sizeof(patchCount));
            deepestLevel = static_cast<const uint32_t*>(data)[2];
            std::memcpy(&pressure, static_cast<const char*>(data) + 3 * sizeof(uint32_t), sizeof(pressure));
            SLANG_RETURN_ON_FAIL(m_device->unmapBuffer(m_lodStatistics));
        }
        ComPtr<ITexture> image;
        m_surface->acquireNextImage(image.writeRef());
        if (!image)
            return SLANG_OK;
        auto size = image->getDesc().size;
        if (!m_output || m_output->getDesc().size.width != size.width || m_output->getDesc().size.height != size.height)
        {
            TextureDesc desc = {};
            desc.type = TextureType::Texture2D;
            desc.size = size;
            desc.format = Format::RGBA32Float;
            desc.usage = TextureUsage::ShaderResource | TextureUsage::UnorderedAccess;
            desc.label = "clusterImage";
            SLANG_RETURN_ON_FAIL(m_device->createTexture(desc, nullptr, m_output.writeRef()));
        }
        m_camera.width = size.width;
        m_camera.height = size.height;
        m_camera.recompute();
        LodSettings lod;
        lod.cameraPosition = m_camera.position;
        lod.cameraRight = m_camera.right;
        lod.cameraUp = m_camera.up;
        lod.cameraForward = m_camera.fwd;
        lod.viewHalfExtent = {math::length(m_camera.imageU), math::length(m_camera.imageV)};
        lod.projectionScale = float(size.height) / (2 * math::length(m_camera.imageV));
        lod.quality = kTargetEdgePixels / m_targetEdgePixels;
        lod.frozen = m_freezeLod;
        lod.adaptive = m_adaptive;
        auto profile = getProfiler().frame();
        SLANG_RETURN_ON_FAIL(profile.begin());
        SLANG_RETURN_ON_FAIL(m_scene.rebuild(m_queue, m_animationTime, m_amplitude, lod));
        ComPtr<ICommandEncoder> encoder;
        SLANG_RETURN_ON_FAIL(m_queue->createCommandEncoder(encoder.writeRef()));
        auto pass = encoder->beginRayTracingPass();
        Profiler::pushRegion(pass, "Ray tracing");
        auto root = pass->bindPipeline(m_renderer.pipeline, m_renderer.table);
        ShaderCursor globals(root);
        SLANG_RETURN_ON_FAIL(globals["tlas"].setBinding(m_scene.tlas));
        SLANG_RETURN_ON_FAIL(globals["surfaceVertices"].setBinding(m_scene.vertices));
        SLANG_RETURN_ON_FAIL(globals["surfaceIndices"].setBinding(m_scene.indices));
        SLANG_RETURN_ON_FAIL(globals["surfacePatches"].setBinding(m_scene.patches));
        SLANG_RETURN_ON_FAIL(globals["output"].setBinding(m_output));
        SLANG_RETURN_ON_FAIL(globals["focusPosition"].setBinding(m_renderer.focus));
        ShaderCursor cursor(root->getEntryPoint(0));
        SLANG_RETURN_ON_FAIL(cursor["cameraPosition"].setData(m_camera.position));
        SLANG_RETURN_ON_FAIL(cursor["imageU"].setData(m_camera.imageU));
        SLANG_RETURN_ON_FAIL(cursor["imageV"].setData(m_camera.imageV));
        SLANG_RETURN_ON_FAIL(cursor["imageW"].setData(m_camera.imageW));
        SLANG_RETURN_ON_FAIL(cursor["viewMode"].setData(m_viewMode));
        pass->dispatchRays(0, size.width, size.height, 1);
        Profiler::popRegion(pass);
        pass->end();
        encoder->copyBuffer(m_lodStatistics, 0, m_scene.counts, 0, 3 * sizeof(uint32_t));
        encoder->copyBuffer(m_lodStatistics, 3 * sizeof(uint32_t), m_scene.allocator, sizeof(uint32_t), sizeof(float));
        SLANG_RETURN_ON_FAIL(m_blitter->blit(image, m_output, encoder));
        clearOverlay();
        printOverlayHeader();
        printCameraControls();
        printOverlay("^3Space^0: Pause bulge | ^3F^0: Freeze LOD | ^3-/=^0: Tessellation | ^3[/]^0: Height\n");
        printOverlay("^3C^0: Cycle views | ^31^0: Shaded | ^32^0: Clusters | ^33^0: Levels | ^34^0: Triangles\n");
        printOverlay("^3T^0: LOD mode | ^3X^0: Approach | ^3Z^0: Detail | ^3R^0: Overview | ^3Scroll^0: Speed\n\n");
        printOverlay(
            "%s | Height: %.2f | Target edge: %.2f px | LOD: %s\n",
            m_paused ? "Bulge paused" : "Moving bulge",
            m_amplitude,
            m_targetEdgePixels,
            m_freezeLod ? "frozen" : "automatic"
        );
        const char* views[] = {"Shaded", "Cluster colors", "Level colors (0 blue -> 12 purple)", "Triangle colors"};
        printOverlay(
            "%s | %s levels 0-%u | Budget: %u patches\n",
            views[m_viewMode],
            m_adaptive ? "Adaptive" : "Uniform",
            m_adaptive ? kMaxLevel : kUniformMaxLevel,
            kPatchBudget
        );
        if (m_haveLodStatistics)
        {
            printOverlay(
                "Last frame: %u patches | Deepest LOD: %u%s\n",
                patchCount,
                deepestLevel,
                deepestLevel == (m_previousAdaptive ? kMaxLevel : kUniformMaxLevel) ? " (depth cap reached)" : ""
            );
            printOverlay(
                "%s: %.2f px | Budget: %.2fx\n",
                m_previousAdaptive ? "Effective target" : "Uniform estimate",
                m_previousTargetEdge * (m_previousAdaptive ? pressure : 1.f),
                m_previousAdaptive ? pressure : 1.f
            );
        }
        printOverlay(
            "Speed: %.4g units/s | Geometry + AS buffers: %.2f MiB\n",
            m_controller.moveSpeed,
            double(m_scene.allocatedBytes) / (1024 * 1024)
        );
        SLANG_RETURN_ON_FAIL(drawOverlay(encoder, image));
        ComPtr<ICommandBuffer> commands;
        SLANG_RETURN_ON_FAIL(encoder->finish(commands.writeRef()));
        SLANG_RETURN_ON_FAIL(m_queue->submit(commands));
        SLANG_RETURN_ON_FAIL(profile.end());
        m_haveLodStatistics = true;
        m_previousTargetEdge = m_targetEdgePixels;
        m_previousAdaptive = m_adaptive;
        if (m_approachSurface)
        {
            // Explicit navigation only: read the center hit after tracing. Normal
            // tree/AS selection stays on the GPU; the regular readback is UI statistics.
            m_approachSurface = false;
            SLANG_RETURN_ON_FAIL(m_queue->waitOnHost());
            math::float4 focus;
            SLANG_RETURN_ON_FAIL(m_device->readBuffer(m_renderer.focus, 0, sizeof(focus), &focus));
            if (focus.w > 0.0001f)
            {
                m_camera.target = {focus.x, focus.y, focus.z};
                m_camera.position += (m_camera.target - m_camera.position) * 0.8f;
                m_controller.moveSpeed = std::clamp(focus.w * 0.1f, 0.0001f, 30.f);
                m_camera.recompute();
                m_controller.reset();
            }
        }
        return presentSurface();
    }

    Result onResize(int width, int height, int framebufferWidth, int framebufferHeight) override
    {
        SLANG_RETURN_ON_FAIL(m_queue->waitOnHost());
        if (framebufferWidth > 0 && framebufferHeight > 0)
        {
            SurfaceConfig config;
            config.width = framebufferWidth;
            config.height = framebufferHeight;
            return configureSurface(m_surface, config);
        }
        else
            return m_surface->unconfigure();
    }

    void onScroll(float x, float y) override
    {
        m_controller.moveSpeed = std::clamp(m_controller.moveSpeed * std::exp2(y * 0.5f), 0.0001f, 30.f);
    }

    void onMousePosition(float x, float y) override { m_controller.onMousePosition(x, y); }
    void onMouseButton(int button, int action, int mods) override { m_controller.onMouseButton(button, action); }
    void onKey(int key, int scancode, int action, int mods) override
    {
        if (m_controller.onKey(key, action) || (action != GLFW_PRESS && action != GLFW_REPEAT))
            return;
        // Half a subdivision step per press; holding the key repeats. The target
        // measures triangle edge length, not triangle area or covered pixels.
        if (key == GLFW_KEY_MINUS || key == GLFW_KEY_KP_SUBTRACT)
        {
            m_targetEdgePixels = std::min(64.f, m_targetEdgePixels * std::sqrt(2.f));
            return;
        }
        if (key == GLFW_KEY_EQUAL || key == GLFW_KEY_KP_ADD)
        {
            m_targetEdgePixels = std::max(0.5f, m_targetEdgePixels / std::sqrt(2.f));
            return;
        }
        if (action != GLFW_PRESS)
            return;
        switch (key)
        {
        case GLFW_KEY_SPACE:
            m_paused = !m_paused;
            break;
        case GLFW_KEY_C:
            m_viewMode = (m_viewMode + 1) % 4;
            break;
        case GLFW_KEY_1:
        case GLFW_KEY_2:
        case GLFW_KEY_3:
        case GLFW_KEY_4:
            m_viewMode = uint32_t(key - GLFW_KEY_1);
            break;
        case GLFW_KEY_T:
            m_adaptive = !m_adaptive;
            break;
        case GLFW_KEY_F:
            m_freezeLod = !m_freezeLod;
            break;
        case GLFW_KEY_LEFT_BRACKET:
            m_amplitude = std::max(0.f, m_amplitude - 0.1f);
            break;
        case GLFW_KEY_RIGHT_BRACKET:
            m_amplitude = std::min(3.f, m_amplitude + 0.1f);
            break;
        case GLFW_KEY_X:
            m_approachSurface = true;
            break;
        case GLFW_KEY_Z:
            resetCamera(true);
            break;
        case GLFW_KEY_R:
            resetCamera();
            break;
        }
    }

    void resetCamera(bool detail = false)
    {
        m_approachSurface = false;
        m_camera.position = detail ? math::float3{3.3f, 2.1f, 3.5f} : math::float3{8, 6, 9};
        m_camera.target = detail ? math::float3{2, 0.55f, 1} : math::float3{0, 0.2f, 0};
        m_controller.moveSpeed = detail ? 0.75f : 3.f;
        m_camera.up = {0, 1, 0};
        m_camera.fov = 45;
        m_camera.recompute();
        m_controller.reset();
    }

    ComPtr<IDevice> m_device;
    ComPtr<ICommandQueue> m_queue;
    ComPtr<ISurface> m_surface;
    ComPtr<ITexture> m_output;
    ComPtr<IBuffer> m_lodStatistics;
    bool m_haveLodStatistics = false;
    bool m_previousAdaptive = true;
    float m_previousTargetEdge = kTargetEdgePixels;
    std::unique_ptr<Blitter> m_blitter;
    Surface m_scene;
    Renderer m_renderer;
    Camera m_camera;
    CameraController m_controller;
    double m_previousTime = -1;
    float m_animationTime = 0;
    float m_amplitude = 1;
    bool m_approachSurface = false;
    bool m_paused = false;
    bool m_freezeLod = false;
    bool m_adaptive = true;
    float m_targetEdgePixels = kTargetEdgePixels;
    uint32_t m_viewMode = 0;
};

EXAMPLE_MAIN(ExampleClusterDisplacement, "cluster-displacement", "Cluster Displacement")
