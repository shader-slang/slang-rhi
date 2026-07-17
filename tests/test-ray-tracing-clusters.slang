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

[shader("raygeneration")]
void rayGenClustersHitObject()
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

    HitObject hit = HitObject.TraceRay(tlas, 0, 0xff, 0, 0, 0, ray, payload);

    if (hit.IsMiss())
        return;

    // Color
    BuiltInTriangleIntersectionAttributes attribs = hit.GetAttributes<BuiltInTriangleIntersectionAttributes>();
    resultTexture[pixel] = float4(attribs.barycentrics, 1.f, 1.f);

    // IDs
    uint index = pixel.y * dim.x + pixel.x;
    ids_buffer[index] = uint2(hit.GetClusterID(), hit.GetPrimitiveIndex());
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

    // TODO: Default initialization doesn't work for structs with bit fields.
    // The workaround is to initialize all fields manually to avoid warnings.
    // https://github.com/shader-slang/slang/issues/11844
    // rhi::TriangleClusterArgs args = {};
    // args.clusterId = idx;
    // args.triangleCount = gTriangleCount;
    // args.vertexCount = gVertexCount;
    // args.vertexBufferStride = uint16_t(gVertexStrideBytes);
    // args.indexBuffer = indexAddr;
    // args.vertexBuffer = vertexAddr;
    // args.indexFormat = rhi::kClusterIndexFormat32bit;
    rhi::TriangleClusterArgs args;
    args.clusterId = idx;
    args.clusterFlags = 0;
    args.triangleCount = gTriangleCount;
    args.vertexCount = gVertexCount;
    args.positionTruncateBitCount = 0;
    args.indexFormat = rhi::kClusterIndexFormat32bit;
    args.opacityMicromapIndexFormat = 0;
    args.baseGeometryIndexAndFlags = 0;
    args.indexBufferStride = uint16_t(0);
    args.vertexBufferStride = uint16_t(gVertexStrideBytes);
    args.geometryIndexAndFlagsBufferStride = uint16_t(0);
    args.opacityMicromapIndexBufferStride = uint16_t(0);
    args.indexBuffer = indexAddr;
    args.vertexBuffer = vertexAddr;
    args.geometryIndexAndFlagsBuffer = 0;
    args.opacityMicromapArray = 0;
    args.opacityMicromapIndexBuffer = 0;
    args.instantiationBoundingBoxLimit = 0;

    gTriArgs[idx] = args;
}
