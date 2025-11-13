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

RWStructuredBuffer<TrianglesArgs> gTriArgs;
uniform uint64_t gIndexBuffer;
uniform uint64_t gVertexBuffer;
uniform uint      gVertexStrideBytes;
uniform uint      gTriangleCount;       // per-cluster
uniform uint      gVertexCount;         // per-cluster
uniform uint      gVertexOffsetElemsPerCluster;
uniform uint      gClusterCount;

[shader("compute")]
[numthreads(64,1,1)]
void writeTriArgs(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= gClusterCount)
        return;
    uint idx = tid.x;
    // Reuse the same index buffer for all clusters (local indices). Offset only vertex base.
    uint64_t indexAddr  = gIndexBuffer;
    uint64_t vertexAddr = gVertexBuffer + uint64_t(idx) * uint64_t(gVertexOffsetElemsPerCluster) * uint64_t(gVertexStrideBytes);

    gTriArgs[idx] = makeTrianglesArgs(
        /*clusterId*/idx,
        /*triangleCount*/gTriangleCount,
        /*vertexCount*/gVertexCount,
        /*indexBuffer*/indexAddr,
        /*vertexBuffer*/vertexAddr,
        /*vertexStrideBytes*/gVertexStrideBytes,
        /*indexFormat*/4
    );
}


