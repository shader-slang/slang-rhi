#include "../../include/slang-rhi-device.h"
// CLUSTER_* bounds and LOD policy are supplied by example-cluster-displacement.cpp.

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
        return;
    }
    // Distance to the displaced surface's conservative bounding box. Looking away
    // never removes patches, so off-screen geometry remains available to shadow rays.
    float3 extent = float3(CLUSTER_SURFACE_SIZE * 0.5, abs(amplitude), CLUSTER_SURFACE_SIZE * 0.5);
    float distance = max(length(max(abs(cameraPosition) - extent, 0)), 0.25);
    float projectedSize = CLUSTER_SURFACE_SIZE * projectionScale / distance;
    float desiredSide = max(projectedSize * quality / (8 * CLUSTER_TARGET_EDGE_PIXELS), 1e-6);
    uint level = min(lodState[0], uint(CLUSTER_MAX_LEVEL));
    if (lodState[1] == 0)
        level = uint(clamp(ceil(log2(desiredSide)), 0.0, float(CLUSTER_MAX_LEVEL)));
    else if (frozen == 0)
    {
        // At most six changes in either direction, even for a camera teleport.
        while (level < CLUSTER_MAX_LEVEL && desiredSide > float(1u << level) * (1 + CLUSTER_LOD_HYSTERESIS))
            ++level;
        while (level > 0 && desiredSide < float(1u << (level - 1)) * (1 - CLUSTER_LOD_HYSTERESIS))
            --level;
    }
    lodState[0] = level;
    lodState[1] = 1;
    counts[0] = 1u << (2 * level);
    counts[1] = 1;
    rhi::ClusterArgs blas;
    blas.clusterHandlesCount = counts[0];
    blas.clusterHandlesStride = 8;
    blas.clusterHandlesBuffer = handlesAddress;
    clusterArgs[0] = blas;
}

uint nodeIndex(uint2 p, uint level)
{
    return ((1u << (2 * level)) - 1) / 3 + p.y * (1u << level) + p.x;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void selectTree(uint3 tid : SV_DispatchThreadID,
    uniform RWStructuredBuffer<uint> tree, uniform StructuredBuffer<uint> lodState,
    uniform uint level, uniform float3 cameraPosition, uniform float projectionScale,
    uniform float quality, uniform uint frozen, uniform float amplitude)
{
    uint side = 1u << level;
    if (tid.x >= side * side || (frozen != 0 && lodState[2] != 0)) return;
    uint2 p = uint2(tid.x % side, tid.x / side);
    uint index = nodeIndex(p, level);
    if (level > 0 && tree[nodeIndex(p >> 1, level - 1)] == 0)
    {
        tree[index] = 0;
        return;
    }
    float size = CLUSTER_SURFACE_SIZE / float(side);
    float2 center = (float2(p) + 0.5) * size - CLUSTER_SURFACE_SIZE * 0.5;
    float3 extent = float3(size * 0.5, abs(amplitude), size * 0.5);
    float distance = max(length(max(abs(cameraPosition - float3(center.x, 0, center.y)) - extent, 0)), 0.25);
    float demand = size * projectionScale * quality / (distance * 8 * CLUSTER_TARGET_EDGE_PIXELS);
    float threshold = lodState[2] == 0 ? 1 : (tree[index] != 0 ? 1 - CLUSTER_LOD_HYSTERESIS : 1 + CLUSTER_LOD_HYSTERESIS);
    tree[index] = demand > threshold ? 1 : 0;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void balanceTree(uint3 tid : SV_DispatchThreadID, uniform RWStructuredBuffer<uint> tree, uniform uint level)
{
    uint side = 1u << level;
    if (tid.x >= side * side) return;
    int2 p = int2(tid.x % side, tid.x / side);
    if (tree[nodeIndex(uint2(p), level)] == 0) return;
    // Reads are at L, atomic writes only at L-1. A barrier separates levels.
    const int2 offsets[5] = {int2(0, 0), int2(-1, 0), int2(1, 0), int2(0, -1), int2(0, 1)};
    for (uint i = 0; i < 5; ++i)
    {
        int2 neighbor = p + offsets[i];
        if (all(neighbor >= 0) && all(neighbor < int(side)))
            InterlockedOr(tree[nodeIndex(uint2(neighbor) >> 1, level - 1)], 1);
    }
}

uint leafLevel(StructuredBuffer<uint> tree, uint2 cell)
{
    uint level = 0;
    while (level < CLUSTER_MAX_LEVEL && tree[nodeIndex(cell >> (CLUSTER_MAX_LEVEL - level), level)] != 0)
        ++level;
    return level;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void compactLeaves(uint3 tid : SV_DispatchThreadID, uniform StructuredBuffer<uint> tree,
    uniform RWStructuredBuffer<uint> counts, uniform RWStructuredBuffer<uint4> patches)
{
    const uint side = 1u << CLUSTER_MAX_LEVEL;
    if (tid.x >= side * side) return;
    uint2 cell = uint2(tid.x % side, tid.x / side);
    uint level = leafLevel(tree, cell);
    uint width = 1u << (CLUSTER_MAX_LEVEL - level);
    if (any((cell & (width - 1)) != 0)) return; // Only the leaf's lower corner emits it.
    uint mask = 0;
    if (cell.x > 0 && leafLevel(tree, cell - uint2(1, 0)) < level) mask |= 1;
    if (cell.x + width < side && leafLevel(tree, cell + uint2(width, 0)) < level) mask |= 2;
    if (cell.y > 0 && leafLevel(tree, cell - uint2(0, 1)) < level) mask |= 4;
    if (cell.y + width < side && leafLevel(tree, cell + uint2(0, width)) < level) mask |= 8;
    uint id;
    InterlockedAdd(counts[0], 1, id);
    patches[id] = uint4(cell / width, level, mask);
}

[shader("compute")]
[numthreads(1, 1, 1)]
void finishSelection(uniform RWStructuredBuffer<uint> lodState, uniform StructuredBuffer<uint> counts,
    uniform RWStructuredBuffer<rhi::ClusterArgs> clusterArgs, uniform uint64_t handlesAddress)
{
    lodState[2] = 1;
    rhi::ClusterArgs blas;
    blas.clusterHandlesCount = counts[0];
    blas.clusterHandlesStride = 8;
    blas.clusterHandlesBuffer = handlesAddress;
    clusterArgs[0] = blas;
}

// All shared boundary positions use the same integer grid coordinates and expression.
[shader("compute")]
[numthreads(128, 1, 1)]
void generateSurface(
    uint3 group : SV_GroupID, uint lane : SV_GroupIndex,
    uniform RWStructuredBuffer<float3> vertices,
    uniform RWStructuredBuffer<uint4> patches,
    uniform RWStructuredBuffer<rhi::TriangleClusterArgs> triangleArgs,
    uniform StructuredBuffer<uint> counts,
    uniform StructuredBuffer<uint> lodState,
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
        float height = amplitude * (0.65 * sin(p.x * 1.4 + time) * cos(p.y * 0.9 - time * 0.7)
                                  + 0.35 * sin(p.x * 2.3 + p.y * 1.7 + time * 1.3));
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
    return colors[min(level, 6u)];
}

[shader("raygeneration")]
void renderRaygen(uniform float3 cameraPosition, uniform float3 imageU, uniform float3 imageV,
                  uniform float3 imageW, uniform uint viewMode)
{
    uint2 pixel = DispatchRaysIndex().xy;
    float2 uv = (float2(pixel) + 0.5) / float2(DispatchRaysDimensions().xy) * 2 - 1;
    RayDesc ray = {cameraPosition, 0.001, normalize(uv.x * imageU - uv.y * imageV + imageW), 1000};
    Payload hit = {};
    TraceRay(tlas, RAY_FLAG_FORCE_OPAQUE, 0xff, 0, 0, 0, ray, hit);
    float3 color = lerp(float3(0.22, 0.30, 0.42), float3(0.55, 0.68, 0.85), saturate(ray.Direction.y * 0.5 + 0.5));
    if (hit.distance >= 0)
    {
        uint base = hit.cluster * 81;
        uint tri = surfacePatches[hit.cluster].w * 128 * 3 + hit.triangle * 3;
        float3 a = surfaceVertices[base + surfaceIndices[tri]];
        float3 b = surfaceVertices[base + surfaceIndices[tri + 1]];
        float3 c = surfaceVertices[base + surfaceIndices[tri + 2]];
        float3 normal = normalize(cross(b - a, c - a));
        if (dot(normal, ray.Direction) > 0) normal = -normal;
        float3 position = ray.Origin + hit.distance * ray.Direction;
        float3 light = normalize(float3(-0.8, 0.55, -0.35));
        RayDesc shadowRay = {position + normal * 0.002, 0.001, light, 1000};
        Payload shadow = {};
        TraceRay(tlas, RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH, 0xff, 0, 0, 0, shadowRay, shadow);
        float3 albedo = float3(0.18, 0.52, 0.36);
        if (viewMode == 1)
        {
            uint3 patch = surfacePatches[hit.cluster].xyz;
            uint seed = patch.x * 1973 + patch.y * 9277 + patch.z * 26699;
            albedo = 0.25 + 0.65 * frac(sin(float3(seed + 1, seed + 17, seed + 113)) * 43758.5453);
        }
        color = albedo * (0.18 + (shadow.distance < 0 ? 0.82 : 0.0) * saturate(dot(normal, light)));
        if (viewMode == 2)
            color = levelColor(surfacePatches[hit.cluster].z);
        else if (viewMode == 3)
        {
            // Barycentric distance to each edge, converted to a roughly pixel-wide line.
            float3 bary = float3(1 - hit.barycentrics.x - hit.barycentrics.y, hit.barycentrics);
            float area = length(cross(b - a, c - a));
            float3 edgeDistance = bary * area / max(float3(length(c - b), length(c - a), length(b - a)), 1e-8);
            float pixelSize = max(2 * hit.distance * length(imageV) / float(DispatchRaysDimensions().y), 1e-6);
            float interior = smoothstep(0.4 * pixelSize, 1.2 * pixelSize, min(edgeDistance.x, min(edgeDistance.y, edgeDistance.z)));
            color = lerp(float3(0.015), color, interior);
        }
    }
    output[pixel] = float4(pow(max(color, 0), 1.0 / 2.2), 1);
}
