// Test shaders for clusters build-and-shoot
// Using relative path to allow Slang language server to find the file.
#include "../include/slang-rhi-device.h"

RaytracingAccelerationStructure tlas;
RWTexture2D<float4> resultTexture;
RWStructuredBuffer<uint2> ids_buffer; // written at every pixel (clusterID, primitiveID)

[raypayload]
struct Payload {
    float3 color : read(caller) : write(caller, closesthit, miss);
}

[shader("miss")]
void missClusters(inout Payload payload)
{
    payload.color = float3(0, 0, 0);
}

[shader("closesthit")]
void closestHitClusters(inout Payload payload, BuiltInTriangleIntersectionAttributes attribs)
{
    payload.color = float3(attribs.barycentrics, 0);
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

RWStructuredBuffer<rhi::TriangleClusterArgs> gTriArgs;
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

    rhi::TriangleClusterArgs args = {};
    args.clusterId = idx;
    args.triangleCount = gTriangleCount;
    args.vertexCount = gVertexCount;
    args.vertexBufferStride = uint16_t(gVertexStrideBytes);
    args.indexBuffer = indexAddr;
    args.vertexBuffer = vertexAddr;
    args.indexFormat = rhi::kClusterIndexFormat32bit;

    gTriArgs[idx] = args;
}
