#include "../../include/slang-rhi-device.h"
// CLUSTER_* bounds and LOD policy are supplied by example-cluster-displacement.cpp.

uint hashUint(uint h)
{
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return h;
}

// Integer hashing keeps the terrain fixed as the camera and subdivision change.
float noiseValue(int2 cell)
{
    uint h = hashUint(uint(cell.x) * 0x8da6b343u ^ uint(cell.y) * 0xd8163841u);
    return float(h >> 8) * (2.0 / 16777215.0) - 1;
}

float terrainNoise(float2 p)
{
    int2 cell = int2(floor(p));
    float2 f = frac(p);
    float2 u = f * f * f * (f * (f * 6 - 15) + 10);
    return lerp(lerp(noiseValue(cell), noiseValue(cell + int2(1, 0)), u.x),
                lerp(noiseValue(cell + int2(0, 1)), noiseValue(cell + int2(1, 1)), u.x), u.y);
}

float rockiness(float2 p)
{
    // A broad, gently rolling basin on one side and increasingly rocky hills on the other.
    return smoothstep(-2.5, 2.5, p.x + 0.25 * p.y);
}

float terrainHeight(float2 p, float spacing, float time)
{
    float detail = 0;
    float frequency = 0.25;
    float2 domain = p;
    float weight = 0.55;
    for (uint octave = 0; octave <= CLUSTER_MAX_LEVEL; ++octave)
    {
        // Frequency describes the noise lattice, not a sinusoid: its values are
        // smoothly interpolated random samples. Keep bands with at least two mesh
        // samples per lattice interval, then fade to one. This approximate footprint
        // filter preserves representable detail without an abrupt octave cutoff.
        if (frequency * spacing >= 1) break;
        float band = 1 - smoothstep(0.5, 1.0, frequency * spacing);
        detail += weight * band * terrainNoise(domain * frequency + float2(13.7, 9.2));
        // Rotate successive octaves so their lattice directions do not line up.
        domain = float2(0.8 * domain.x - 0.6 * domain.y, 0.6 * domain.x + 0.8 * domain.y);
        frequency *= 2;
        weight *= 0.5;
    }
    float rough = rockiness(p);
    float height = -0.25 + 0.75 * rough + 0.15 * sin(p.y * 0.35);
    height += lerp(0.08, 1.0, rough) * detail;

    // Only this localized bulge moves; fine terrain detail stays anchored in world space.
    // Broaden and attenuate it on coarse meshes to avoid undersampling the deformation.
    float2 center = float2(2.8 * cos(time * 0.24), 2.0 * sin(time * 0.31));
    float radiusSquared = 1.2 * 1.2 + 2 * spacing * spacing;
    float2 offset = p - center;
    height += 0.35 * (1.2 * 1.2 / radiusSquared) * exp(-dot(offset, offset) / radiusSquared);
    return height;
}

// Base <= 0.65, all octave weights sum to < 1.1, and the bulge <= 0.35.
// The negative bound is smaller. Keep LOD selection conservative at every amplitude.
static const float kTerrainHeightBound = 2.1;

[shader("compute")]
[numthreads(1, 1, 1)]
void selectLevel(
    uniform RWStructuredBuffer<uint> lodState,
    uniform RWStructuredBuffer<uint> counts,
    uniform RWStructuredBuffer<rhi::ClusterArgs> clusterArgs,
    uniform uint64_t handlesAddress,
    uniform float3 cameraPosition, uniform float projectionScale, uniform float quality,
    uniform uint frozen, uniform float amplitude, uniform uint adaptive)
{
    if (adaptive != 0)
    {
        counts[0] = 0;
        counts[1] = 1;
        counts[2] = 0;
        return;
    }
    // Distance to the displaced surface's conservative bounding box. Looking away
    // never removes patches, so off-screen geometry remains available to shadow rays.
    float3 extent = float3(CLUSTER_SURFACE_SIZE * 0.5, kTerrainHeightBound * abs(amplitude), CLUSTER_SURFACE_SIZE * 0.5);
    float distance = max(length(max(abs(cameraPosition) - extent, 0)), 0.25);
    float projectedSize = CLUSTER_SURFACE_SIZE * projectionScale / distance;
    float desiredSide = max(projectedSize * quality / (8 * CLUSTER_TARGET_EDGE_PIXELS), 1e-6);
    uint level = min(lodState[0], uint(CLUSTER_UNIFORM_MAX_LEVEL));
    if (lodState[1] == 0)
        level = uint(clamp(ceil(log2(desiredSide)), 0.0, float(CLUSTER_UNIFORM_MAX_LEVEL)));
    else if (frozen == 0)
    {
        // At most six changes in either direction, even for a camera teleport.
        while (level < CLUSTER_UNIFORM_MAX_LEVEL && desiredSide > float(1u << level) * (1 + CLUSTER_LOD_HYSTERESIS))
            ++level;
        while (level > 0 && desiredSide < float(1u << (level - 1)) * (1 - CLUSTER_LOD_HYSTERESIS))
            --level;
    }
    lodState[0] = level;
    lodState[1] = 1;
    counts[0] = 1u << (2 * level);
    counts[1] = 1;
    counts[2] = level;
    rhi::ClusterArgs blas;
    blas.clusterHandlesCount = counts[0];
    blas.clusterHandlesStride = 8;
    blas.clusterHandlesBuffer = handlesAddress;
    clusterArgs[0] = blas;
}

// Persistent sparse nodes: xy = patch coordinate, z = level, w = first child.
// Zero denotes a leaf; ~0u denotes an unused node. Node zero is always the root.
static const uint kBlockCount = (CLUSTER_PATCH_BUDGET - 1) / 3;
static const uint kNodeCount = 1 + 4 * kBlockCount;
static const uint kFinestSide = 1u << CLUSTER_MAX_LEVEL;

uint findLeaf(StructuredBuffer<uint4> tree, uint2 cell)
{
    uint index = 0;
    for (uint level = 0; level < CLUSTER_MAX_LEVEL; ++level)
    {
        uint child = tree[index].w;
        if (child == 0) break;
        uint2 quadrant = (cell >> (CLUSTER_MAX_LEVEL - level - 1)) & 1;
        index = child + quadrant.y * 2 + quadrant.x;
    }
    return index;
}

uint leafLevel(StructuredBuffer<uint4> tree, uint2 cell)
{
    return tree[findLeaf(tree, cell)].z;
}

// Two samples per face also detect the finer neighbors of a merge candidate.
// An out-of-domain neighbor is represented by the node itself.
uint neighborLeaf(StructuredBuffer<uint4> tree, uint4 node, uint edge, uint half)
{
    int width = 1 << (CLUSTER_MAX_LEVEL - node.z);
    int2 origin = int2(node.xy) * width;
    int along = half * (width / 2);
    int2 cell = edge < 2 ? origin + int2(edge == 0 ? -1 : width, along)
                        : origin + int2(along, edge == 2 ? -1 : width);
    cell = clamp(cell, int2(0), int2(kFinestSide - 1));
    return findLeaf(tree, uint2(cell));
}

// Camera-space triangle edges, with conservative rejection against the view frustum.
// Retain triangles crossing the eye plane with a screen-sized demand instead of
// dividing by zero. Off-screen patches remain in the tree for secondary rays.
float projectedEdge(float3 a, float3 b, float3 c, float2 viewHalfExtent, float projectionScale)
{
    if (max(a.z, max(b.z, c.z)) <= 0) return 0;
    for (uint axis = 0; axis < 2; ++axis)
    {
        if (min(a[axis] - viewHalfExtent[axis] * a.z,
            min(b[axis] - viewHalfExtent[axis] * b.z, c[axis] - viewHalfExtent[axis] * c.z)) > 0) return 0;
        if (max(a[axis] + viewHalfExtent[axis] * a.z,
            max(b[axis] + viewHalfExtent[axis] * b.z, c[axis] + viewHalfExtent[axis] * c.z)) < 0) return 0;
    }
    if (min(a.z, min(b.z, c.z)) <= 1e-7) return 2 * length(viewHalfExtent) * projectionScale;
    float2 pa = a.xy / a.z, pb = b.xy / b.z, pc = c.xy / c.z;
    return sqrt(max(dot(pa - pb, pa - pb), max(dot(pb - pc, pb - pc), dot(pc - pa, pc - pa)))) * projectionScale;
}

groupshared float3 selectionVertices[81];
groupshared uint selectionActive, selectionMask, selectionAllowed, selectionEdge;

[shader("compute")]
[numthreads(128, 1, 1)]
void selectTree(uint3 group : SV_GroupID, uint lane : SV_GroupIndex,
    uniform StructuredBuffer<uint4> tree, uniform RWStructuredBuffer<uint2> requests,
    uniform StructuredBuffer<uint> indices, uniform StructuredBuffer<uint2> previousSplits,
    uniform StructuredBuffer<uint> allocator, uniform uint splitting,
    uniform float3 cameraPosition, uniform float3 cameraRight, uniform float3 cameraUp,
    uniform float3 cameraForward, uniform float2 viewHalfExtent,
    uniform float projectionScale, uniform float quality, uniform float amplitude, uniform float time)
{
    uint id = group.x;
    uint4 node = tree[id];
    if (lane == 0)
    {
        requests[id] = 0;
        selectionActive = node.w != ~0u && (splitting != 0 ? node.w == 0 && node.z < CLUSTER_MAX_LEVEL : node.w != 0);
        selectionMask = selectionEdge = 0;
        selectionAllowed = 1;
        if (selectionActive != 0)
        {
            // Keep the preceding split pass's balance dependencies through the
            // merge pass. Otherwise off-screen neighbors repeatedly merge back
            // just after visible fine patches force them to split.
            if (splitting == 0 && previousSplits[id].x != 0) selectionActive = 0;
            if (splitting == 0)
                for (uint child = 0; child < 4; ++child)
                    if (tree[node.w + child].w != 0) selectionActive = 0;
            for (uint edge = 0; edge < 4; ++edge)
            {
                uint level = tree[neighborLeaf(tree, node, edge, 0)].z;
                if (level < node.z)
                {
                    selectionMask |= 1u << edge;
                    selectionAllowed = 0;
                }
                if (splitting == 0)
                    for (uint half = 0; half < 2; ++half)
                    {
                        uint neighbor = neighborLeaf(tree, node, edge, half);
                        if (tree[neighbor].z > node.z + 1 || previousSplits[neighbor].x != 0) selectionActive = 0;
                    }
            }
        }
    }
    GroupMemoryBarrierWithGroupSync();
    if (selectionActive == 0) return;
    if (lane < 81)
    {
        uint2 coord = (node.xy * 8 + uint2(lane % 9, lane / 9)) << (CLUSTER_MAX_LEVEL - node.z);
        float2 p = float2(coord) * (CLUSTER_SURFACE_SIZE / float(kFinestSide * 8)) - CLUSTER_SURFACE_SIZE * 0.5;
        // A merge uses the prospective parent's footprint; a split measures the
        // current leaf exactly, including the shared filter field at transitions.
        float spacing = max(displacementSpacing(coord, tree), (CLUSTER_SURFACE_SIZE / 8.0) * exp2(-float(node.z)));
        float3 v = float3(p.x, amplitude * terrainHeight(p, spacing, time), p.y) - cameraPosition;
        selectionVertices[lane] = float3(dot(v, cameraRight), dot(v, cameraUp), dot(v, cameraForward));
    }
    GroupMemoryBarrierWithGroupSync();
    if (lane < 128 - 4 * countbits(selectionMask))
    {
        uint base = selectionMask * 128 * 3 + lane * 3;
        float edge = projectedEdge(selectionVertices[indices[base]], selectionVertices[indices[base + 1]],
            selectionVertices[indices[base + 2]], viewHalfExtent, projectionScale);
        InterlockedMax(selectionEdge, asuint(edge));
    }
    GroupMemoryBarrierWithGroupSync();
    if (lane == 0)
    {
        float demand = asfloat(selectionEdge) * quality / CLUSTER_TARGET_EDGE_PIXELS;
        float pressure = asfloat(allocator[1]);
        if (splitting != 0)
            requests[id] = uint2(demand > pressure * (1 + CLUSTER_LOD_HYSTERESIS) ? asuint(demand) : 0, selectionAllowed);
        else if (demand < pressure * (1 - CLUSTER_LOD_HYSTERESIS))
            requests[id] = uint2(1, 0);
    }
}

[shader("compute")]
[numthreads(64, 1, 1)]
void balanceTree(uint3 tid : SV_DispatchThreadID, uniform StructuredBuffer<uint4> tree,
    uniform RWStructuredBuffer<uint2> requests, uniform uint level)
{
    uint id = tid.x;
    if (id >= kNodeCount) return;
    uint4 node = tree[id];
    if (node.z != level || node.w != 0) return;
    uint priority = requests[id].x;
    if (priority == 0) return;
    // Descending passes forward blocked requests all the way to their coarse
    // dependencies. Eligibility was computed from the immutable pre-split tree.
    for (uint edge = 0; edge < 4; ++edge)
    {
        uint neighbor = neighborLeaf(tree, node, edge, 0);
        if (tree[neighbor].z < level) InterlockedMax(requests[neighbor].x, priority);
    }
}

// Quarter-octave buckets prioritize large errors; ties share the remaining budget.
uint priorityBin(uint priority)
{
    return uint(clamp(floor(log2(asfloat(priority)) * 4), 0.0, 63.0));
}

groupshared uint allocationBins[64];
groupshared uint allocationCount;
groupshared uint allocationAvailable;

[shader("compute")]
[numthreads(256, 1, 1)]
void allocateTree(uint lane : SV_GroupIndex,
    uniform RWStructuredBuffer<uint4> tree, uniform StructuredBuffer<uint2> requests,
    uniform RWStructuredBuffer<uint> freeBlocks, uniform RWStructuredBuffer<uint> allocator,
    uniform uint splitting)
{
    // One cooperative group scans the pool in parallel. Queries have finished before
    // this dispatch, and all accepted edits are disjoint. Only the 64-bin prefix sum
    // and the two allocator counters need a single lane; no serial scan of the pool.
    if (lane == 0)
    {
        allocationAvailable = allocator[0];
        allocationCount = 0;
    }
    if (lane < 64) allocationBins[lane] = 0;
    GroupMemoryBarrierWithGroupSync();
    uint available = allocationAvailable;
    if (splitting == 0)
    {
        for (uint id = lane; id < kNodeCount; id += 256)
        {
            if (requests[id].x == 0) continue;
            uint slot;
            InterlockedAdd(allocationCount, 1, slot);
            uint child = tree[id].w;
            for (uint i = 0; i < 4; ++i) tree[child + i].w = ~0u;
            tree[id].w = 0;
            freeBlocks[available + slot] = child;
        }
        GroupMemoryBarrierWithGroupSync();
        if (lane == 0) allocator[0] = available + allocationCount;
    }
    else
    {
        for (uint id = lane; id < kNodeCount; id += 256)
        {
            uint2 request = requests[id];
            if (request.x != 0 && request.y != 0)
                InterlockedAdd(allocationBins[priorityBin(request.x)], 1);
        }
        GroupMemoryBarrierWithGroupSync();
        if (lane == 0)
            for (int bin = 63; bin >= 0; --bin)
            {
                uint count = allocationBins[bin];
                allocationBins[bin] = allocationCount;
                allocationCount += count;
            }
        GroupMemoryBarrierWithGroupSync();
        for (uint id = lane; id < kNodeCount; id += 256)
        {
            uint2 request = requests[id];
            if (request.x == 0 || request.y == 0) continue;
            uint rank;
            InterlockedAdd(allocationBins[priorityBin(request.x)], 1, rank);
            // Each bin owns a distinct range, with higher priorities first. Truncating
            // the ranges at available admits exactly the number of blocks we can afford.
            if (rank >= available) continue;
            uint child = freeBlocks[available - 1 - rank];
            uint4 node = tree[id];
            for (uint i = 0; i < 4; ++i)
                tree[child + i] = uint4(node.xy * 2 + uint2(i & 1, i >> 1), node.z + 1, 0);
            tree[id].w = child;
        }
        if (lane == 0)
        {
            uint remaining = available - min(available, allocationCount);
            // A full pool raises the target gradually to reclaim distant patches.
            float pressure = asfloat(allocator[1]);
            if (allocationCount > available) pressure = min(pressure * 1.08, 65536.0);
            else if (remaining > kBlockCount / 4) pressure = max(1.0, pressure * 0.97);
            allocator[0] = remaining;
            allocator[1] = asuint(pressure);
        }
    }
}

[shader("compute")]
[numthreads(64, 1, 1)]
void compactLeaves(uint3 tid : SV_DispatchThreadID, uniform StructuredBuffer<uint4> tree,
    uniform RWStructuredBuffer<uint> counts, uniform RWStructuredBuffer<uint4> patches)
{
    if (tid.x >= kNodeCount) return;
    uint4 node = tree[tid.x];
    if (node.w != 0) return;
    uint mask = 0;
    for (uint edge = 0; edge < 4; ++edge)
        if (tree[neighborLeaf(tree, node, edge, 0)].z < node.z) mask |= 1u << edge;
    uint id;
    InterlockedMax(counts[2], node.z);
    InterlockedAdd(counts[0], 1, id);
    patches[id] = uint4(node.xyz, mask);
}

[shader("compute")]
[numthreads(1, 1, 1)]
void finishSelection(uniform StructuredBuffer<uint> counts,
    uniform RWStructuredBuffer<rhi::ClusterArgs> clusterArgs, uniform uint64_t handlesAddress)
{
    rhi::ClusterArgs blas;
    blas.clusterHandlesCount = counts[0];
    blas.clusterHandlesStride = 8;
    blas.clusterHandlesBuffer = handlesAddress;
    clusterArgs[0] = blas;
}

float displacementSpacing(uint2 coord, StructuredBuffer<uint4> tree)
{
    // Extend each leaf's footprint two of its mesh cells beyond its boundary.
    // Taking the largest contribution is conservative inside every leaf and gives
    // one continuous field for all patches, including diagonal neighbors. The
    // transition width follows the coarse mesh rather than the maximum tree depth.
    float2 position = float2(coord) / 8.0;
    float spacing = 0;
    uint stack[3 * CLUSTER_MAX_LEVEL + 1];
    uint count = 1;
    stack[0] = 0;
    while (count != 0)
    {
        uint4 node = tree[stack[--count]];
        float width = float(1u << (CLUSTER_MAX_LEVEL - node.z));
        float2 low = float2(node.xy) * width;
        float2 distance = max(max(low - position, position - (low + width)), 0.0);
        float footprint = width / 8;
        float contribution = footprint * (1 - smoothstep(0.0, 2 * footprint, length(distance)));
        // Descendants have smaller footprints, smaller influence radii, and boxes
        // contained in this one. This bound prunes subtrees that cannot affect the result.
        if (contribution <= spacing) continue;
        if (node.w == 0)
            spacing = contribution;
        else
            for (uint child = 0; child < 4; ++child)
                stack[count++] = node.w + child;
    }
    return spacing * (CLUSTER_SURFACE_SIZE / float(kFinestSide));
}

// Shared positions and filter footprints depend only on canonical integer coordinates.
[shader("compute")]
[numthreads(128, 1, 1)]
void generateSurface(
    uint3 group : SV_GroupID, uint lane : SV_GroupIndex,
    uniform RWStructuredBuffer<float3> vertices,
    uniform RWStructuredBuffer<uint4> patches,
    uniform RWStructuredBuffer<rhi::TriangleClusterArgs> triangleArgs,
    uniform StructuredBuffer<uint> counts,
    uniform StructuredBuffer<uint> lodState,
    uniform StructuredBuffer<uint4> tree,
    uniform uint64_t vertexAddress, uniform uint64_t indexAddress,
    uniform float time, uniform float amplitude, uniform uint adaptive)
{
    uint id = group.x;
    uint count = counts[0];
    if (id >= count) return;
    uint4 metadata = adaptive != 0 ? patches[id] : uint4(id % (1u << lodState[0]), id / (1u << lodState[0]), lodState[0], 0);
    uint level = metadata.z;
    uint2 patch = metadata.xy;
    if (lane < 81)
    {
        uint2 coord = (patch * 8 + uint2(lane % 9, lane / 9)) << (CLUSTER_MAX_LEVEL - level);
        float2 p = float2(coord) * (CLUSTER_SURFACE_SIZE / float((1u << CLUSTER_MAX_LEVEL) * 8)) - CLUSTER_SURFACE_SIZE * 0.5;
        float spacing = adaptive != 0 ? displacementSpacing(coord, tree) : (CLUSTER_SURFACE_SIZE / 8.0) * exp2(-float(level));
        float height = amplitude * terrainHeight(p, spacing, time);
        vertices[id * 81 + lane] = float3(p.x, height, p.y);
    }
    if (lane != 0) return;
    // Adaptive metadata is already complete. Do not write it while other lanes
    // in this group are still reading it.
    if (adaptive == 0) patches[id] = metadata;
    // Explicit initialization avoids Slang's default-initialization issue with bit fields.
    rhi::TriangleClusterArgs args;
    args.clusterId = id;
    args.clusterFlags = 0;
    args.triangleCount = 128 - 4 * countbits(metadata.w);
    args.vertexCount = 81;
    args.positionTruncateBitCount = 0;
    args.indexFormat = rhi::kClusterIndexFormat32bit;
    args.opacityMicromapIndexFormat = 0;
    args.baseGeometryIndexAndFlags = rhi::kClusterGeometryFlagOpaque;
    args.indexBufferStride = uint16_t(0);
    args.vertexBufferStride = uint16_t(12);
    args.geometryIndexAndFlagsBufferStride = uint16_t(0);
    args.opacityMicromapIndexBufferStride = uint16_t(0);
    args.indexBuffer = indexAddress + uint64_t(metadata.w) * 128 * 3 * 4;
    args.vertexBuffer = vertexAddress + uint64_t(id) * 81 * 12;
    args.geometryIndexAndFlagsBuffer = 0;
    args.opacityMicromapArray = 0;
    args.opacityMicromapIndexBuffer = 0;
    args.instantiationBoundingBoxLimit = 0;
    triangleArgs[id] = args;
}

[shader("compute")]
[numthreads(1, 1, 1)]
void writeInstance(uniform ByteAddressBuffer handles, uniform RWByteAddressBuffer instances, uniform uint handleOffset)
{
    instances.Store2(handleOffset, handles.Load2(0));
}

RaytracingAccelerationStructure tlas;
StructuredBuffer<float3> surfaceVertices;
StructuredBuffer<uint> surfaceIndices;
StructuredBuffer<uint4> surfacePatches;
RWTexture2D<float4> output;
RWStructuredBuffer<float4> focusPosition;

[raypayload]
struct Payload
{
    float distance : read(caller) : write(caller, closesthit, miss);
    uint cluster : read(caller) : write(caller, closesthit, miss);
    uint triangle : read(caller) : write(caller, closesthit, miss);
    float2 barycentrics : read(caller) : write(caller, closesthit, miss);
};

[shader("miss")]
void mainMiss(inout Payload payload)
{
    payload.distance = -1;
    payload.cluster = payload.triangle = 0xffffffff;
    payload.barycentrics = 0;
}

[shader("closesthit")]
void mainClosestHit(inout Payload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    payload.distance = RayTCurrent();
    payload.cluster = GetClusterID();
    payload.triangle = PrimitiveIndex();
    payload.barycentrics = attributes.barycentrics;
}

float3 levelColor(uint level)
{
    const float3 colors[7] = {
        float3(0.15, 0.3, 0.85), float3(0.1, 0.65, 0.85), float3(0.1, 0.7, 0.35),
        float3(0.7, 0.8, 0.15), float3(0.95, 0.5, 0.1), float3(0.85, 0.15, 0.1),
        float3(0.65, 0.15, 0.8)
    };
    float t = float(level) * 6 / CLUSTER_MAX_LEVEL;
    uint i = min(uint(t), 5u);
    return lerp(colors[i], colors[i + 1], t - float(i));
}

[shader("raygeneration")]
void renderRaygen(uniform float3 cameraPosition, uniform float3 imageU, uniform float3 imageV,
                  uniform float3 imageW, uniform uint viewMode)
{
    uint2 pixel = DispatchRaysIndex().xy;
    float2 uv = (float2(pixel) + 0.5) / float2(DispatchRaysDimensions().xy) * 2 - 1;
    float rayMin = max(1e-6, max(abs(cameraPosition.x), max(abs(cameraPosition.y), abs(cameraPosition.z))) * 5e-7);
    RayDesc ray = {cameraPosition, rayMin, normalize(uv.x * imageU - uv.y * imageV + imageW), 1000};
    Payload hit = {};
    TraceRay(tlas, RAY_FLAG_FORCE_OPAQUE, 0xff, 0, 0, 0, ray, hit);
    float3 color = lerp(float3(0.22, 0.30, 0.42), float3(0.55, 0.68, 0.85), saturate(ray.Direction.y * 0.5 + 0.5));
    if (hit.distance >= 0)
    {
        uint4 patch = surfacePatches[hit.cluster];
        if (viewMode == 2)
            color = levelColor(patch.z);
        else if (viewMode == 3)
        {
            // Primitive IDs are local to a cluster. Include its stable coordinates,
            // level, and stitch pattern rather than the transient compacted cluster ID.
            uint seed = hashUint(patch.x ^ hashUint(patch.y) ^ (patch.z * 0x9e3779b9u) ^ (patch.w * 0x85ebca6bu));
            uint h = hashUint(seed ^ hit.triangle);
            // Unlit solid colors keep adjacent triangles readable even in shadow.
            color = 0.1 + 0.8 * float3(h & 255, (h >> 8) & 255, (h >> 16) & 255) / 255.0;
        }
        else
        {
            uint base = hit.cluster * 81;
            uint tri = patch.w * 128 * 3 + hit.triangle * 3;
            float3 a = surfaceVertices[base + surfaceIndices[tri]];
            float3 b = surfaceVertices[base + surfaceIndices[tri + 1]];
            float3 c = surfaceVertices[base + surfaceIndices[tri + 2]];
            float3 normal = normalize(cross(b - a, c - a));
            if (dot(normal, ray.Direction) > 0) normal = -normal;
            float3 position = a * (1 - hit.barycentrics.x - hit.barycentrics.y) + b * hit.barycentrics.x + c * hit.barycentrics.y;
            float3 light = normalize(float3(-0.8, 0.55, -0.35));
            float spacing = (CLUSTER_SURFACE_SIZE / 8.0) * exp2(-float(patch.z));
            float error = max(abs(position.x), max(abs(position.y), abs(position.z))) * 8e-7;
            float bias = max(error, spacing * 0.002);
            RayDesc shadowRay = {position + normal * bias, bias * 0.25, light, 1000};
            Payload shadow = {};
            TraceRay(tlas, RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH, 0xff, 0, 0, 0, shadowRay, shadow);
            float3 albedo = lerp(float3(0.16, 0.29, 0.09), float3(0.46, 0.34, 0.21), rockiness(position.xz));
            float slope = 1 - abs(normal.y);
            albedo = lerp(albedo, float3(0.43, 0.45, 0.46), smoothstep(0.12, 0.55, slope));
            if (viewMode == 1)
            {
                uint seed = patch.x * 1973 + patch.y * 9277 + patch.z * 26699;
                albedo = 0.25 + 0.65 * frac(sin(float3(seed + 1, seed + 17, seed + 113)) * 43758.5453);
            }
            color = albedo * (0.18 + (shadow.distance < 0 ? 0.82 : 0.0) * saturate(dot(normal, light)));
        }
    }
    if (all(pixel == DispatchRaysDimensions().xy / 2))
        focusPosition[0] = float4(ray.Origin + max(hit.distance, 0.0) * ray.Direction, hit.distance);
    // The shared blitter handles the surface format's sRGB encoding.
    output[pixel] = float4(max(color, 0), 1);
}
