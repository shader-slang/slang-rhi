// Test shaders for clusters build-and-shoot
#include "../shaders/cluster_accel/cluster_accel_abi.slang"

RaytracingAccelerationStructure tlas;
RWTexture2D<float4> resultTexture;
RWStructuredBuffer<uint2> ids_buffer; // written at every pixel (clusterID, primitiveID)

struct Payload {
    float3 color;
}

[shader("miss")]
void missClusters(inout Payload payload)
{
    payload.color = float3(0, 0, 0);
}

[shader("closesthit")]
void closestHitClusters(inout Payload payload, BuiltInTriangleIntersectionAttributes attribs)
{
    // Write a constant color for "hit band" detection
    payload.color = float3(1, 1, 1);
    uint2 pix = DispatchRaysIndex().xy;
    uint2 dims = DispatchRaysDimensions().xy;
    uint index = pix.y * dims.x + pix.x;
    ids_buffer[index] = uint2(GetClusterID(), PrimitiveIndex());
}

[shader("raygeneration")]
void rayGenClusters()
{
    uint2 pixel = DispatchRaysIndex().xy;
    uint2 dim = DispatchRaysDimensions().xy;

    float2 uv = float2(pixel) / float2(dim - 1);

    RayDesc ray;
    ray.Origin = float3(uv * 2 - 1, 1);
    ray.Direction = float3(0, 0, -1);
    ray.TMin = 0;
    ray.TMax = 2;

    Payload payload = {};

    TraceRay(tlas, 0, 0xff, 0, 0, 0, ray, payload);

    resultTexture[pixel] = float4(payload.color, 1.f);
}

// --- Compute: write TrianglesArgs records on device ---
using namespace cluster_abi;

RWStructuredBuffer<TrianglesArgs> g_tri_args;
uniform uint64_t g_index_buffer;
uniform uint64_t g_vertex_buffer;
uniform uint      g_vertex_stride_bytes;
uniform uint      g_triangle_count;       // per-cluster
uniform uint      g_vertex_count;         // per-cluster
uniform uint      g_vertex_offset_elems_per_cluster;
uniform uint      g_cluster_count;

[shader("compute")]
[numthreads(64,1,1)]
void write_tri_args(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= g_cluster_count)
        return;
    uint idx = tid.x;
    // Reuse the same index buffer for all clusters (local indices). Offset only vertex base.
    uint64_t indexAddr  = g_index_buffer;
    uint64_t vertexAddr = g_vertex_buffer + uint64_t(idx) * uint64_t(g_vertex_offset_elems_per_cluster) * uint64_t(g_vertex_stride_bytes);

    g_tri_args[idx] = makeTrianglesArgs(
        /*clusterId*/idx,
        /*triangleCount*/g_triangle_count,
        /*vertexCount*/g_vertex_count,
        /*indexBuffer*/indexAddr,
        /*vertexBuffer*/vertexAddr,
        /*vertexStrideBytes*/g_vertex_stride_bytes,
        /*indexFormat*/4
    );
}


