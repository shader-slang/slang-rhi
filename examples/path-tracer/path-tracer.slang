#include "settings.h"

#ifndef USE_RAYTRACING_PIPELINE
#warning "Missing USE_RAYTRACING_PIPELINE define"
#define USE_RAYTRACING_PIPELINE 0
#endif

#include "path-tracer-common.slang"

struct Scene
{
    RaytracingAccelerationStructure tlas;

    StructuredBuffer<MaterialDesc> materialDescs;
    StructuredBuffer<MeshDesc> meshDescs;
    StructuredBuffer<InstanceDesc> instanceDescs;
    StructuredBuffer<Vertex> vertices;
    StructuredBuffer<uint> indices;
    StructuredBuffer<float4x4> transforms;
    StructuredBuffer<float4x4> inverseTransposeTransforms;

    Camera camera;

    MaterialDesc getMaterial(HitInfo hitInfo)
    {
        InstanceDesc instanceDesc = g_scene.instanceDescs[hitInfo.instanceID];
        return g_scene.materialDescs[instanceDesc.materialID];
    }

    Vertex getVertex(HitInfo hitInfo) { return getVertex(hitInfo.instanceID, hitInfo.primitiveIndex, hitInfo.bary); }

    Vertex getVertex(uint instanceID, uint primitiveIndex, float2 bary)
    {
        InstanceDesc instanceDesc = g_scene.instanceDescs[instanceID];
        MeshDesc meshDesc = g_scene.meshDescs[instanceDesc.meshID];
        float4x4 transform = g_scene.transforms[instanceDesc.transformID];
        float4x4 inverseTransposeTransform = g_scene.inverseTransposeTransforms[instanceDesc.transformID];
        uint i0 = g_scene.indices[meshDesc.indexOffset + primitiveIndex * 3 + 0];
        uint i1 = g_scene.indices[meshDesc.indexOffset + primitiveIndex * 3 + 1];
        uint i2 = g_scene.indices[meshDesc.indexOffset + primitiveIndex * 3 + 2];
        Vertex v0 = g_scene.vertices[meshDesc.vertexOffset + i0];
        Vertex v1 = g_scene.vertices[meshDesc.vertexOffset + i1];
        Vertex v2 = g_scene.vertices[meshDesc.vertexOffset + i2];
        float3 b = float3(1.0 - bary.x - bary.y, bary.x, bary.y);
        return {
            mul(transform, float4(b.x * v0.position + b.y * v1.position + b.z * v2.position, 1.0)).xyz,
            normalize(mul(inverseTransposeTransform, float4(b.x * v0.normal + b.y * v1.normal + b.z * v2.normal, 0)).xyz
            ),
            b.x * v0.uv + b.y * v1.uv + b.z * v2.uv,
        };
    }
};

float3 envmap(float3 dir)
{
    return float3(1);
    // float3 env = float3(0);
    // env += saturate(dot(dir, float3(0, 1, 0))) * float3(0.5, 0.5, 1);
    // const float3 LIGHT_DIR = normalize(float3(0.5, 1, -0.8));
    // env += saturate(smoothstep(0.95, 0.96, dot(dir, LIGHT_DIR))) * float3(10);
    // return env;
}

ParameterBlock<Scene> g_scene;

#if USE_RAYTRACING_PIPELINE

[shader("miss")]
void mainMiss(inout Path path)
{
    path.L += path.thp * envmap(path.ray.dir);
}

[shader("closesthit")]
void mainClosestHit(inout Path path, BuiltInTriangleIntersectionAttributes attribs)
{
    HitInfo hit = {
        true,
        attribs.barycentrics,
        // RayTMin(),
        InstanceID(),
        PrimitiveIndex(),
    };
    scatter(path, g_scene.getVertex(hit), g_scene.getMaterial(hit));
    if (path.vertexIndex >= PATH_TRACER_MAX_BOUNCES)
    {
        return;
    }
    TraceRay(
        g_scene.tlas,
        0,
        0xff,
        0 /* RayContributionToHitGroupIndex */,
        0 /* MultiplierForGeometryContributionHitGroupIndex */,
        0 /* MissShaderIndex */,
        path.ray.toRayDesc(),
        path
    );
}

[shader("raygeneration")]
void renderRaygen(RWTexture2D<float4> output, uniform uint frame)
{
    uint2 pixel = DispatchRaysIndex().xy;
    uint2 dim;
    output.GetDimensions(dim.x, dim.y);
    if (any(pixel >= dim))
        return;

    const uint spp = PATH_TRACER_SAMPLES_PER_PIXEL;
    float3 L = float3(0);
    for (uint i = 0; i < spp; i++)
    {
        RNG rng = RNG(pixel, frame * spp + i);
        float2 uv = float2(pixel + rng.next2D()) / float2(dim);
        uv.y = 1 - uv.y;
        Ray ray = g_scene.camera.get_ray(uv);
        Path path = Path(pixel, ray, rng);
        TraceRay(
            g_scene.tlas,
            0,
            0xff,
            0 /* RayContributionToHitGroupIndex */,
            0 /* MultiplierForGeometryContributionHitGroupIndex */,
            0 /* MissShaderIndex */,
            path.ray.toRayDesc(),
            path
        );
        L += path.L;
    }
    L /= spp;
    output[pixel] = float4(L, 1);
}

#else // USE_RAYTRACING_PIPELINE

HitInfo intersect(Ray ray)
{
    HitInfo hit = {};

    RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> q;
    q.TraceRayInline(g_scene.tlas, 0, 0xff, ray.toRayDesc());
    while (q.Proceed())
        ;
    if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
    {
        hit = {
            true,
            q.CommittedTriangleBarycentrics(),
            // q.CommittedT(),
            q.CommittedInstanceID(),
            q.CommittedPrimitiveIndex(),
        };
    }
    return hit;
}

void tracePath(inout Path path)
{
    while (path.vertexIndex < PATH_TRACER_MAX_BOUNCES)
    {
        HitInfo hit = intersect(path.ray);
        if (!hit.valid)
        {
            path.L += path.thp * envmap(path.ray.dir);
            break;
        }
        scatter(path, g_scene.getVertex(hit), g_scene.getMaterial(hit));
    }
}

[shader("compute")]
[numthreads(8, 8, 1)]
void renderCompute(uint3 tid: SV_DispatchThreadID, RWTexture2D<float4> output, uniform uint frame)
{
    uint2 pixel = tid.xy;
    uint2 dim;
    output.GetDimensions(dim.x, dim.y);
    if (any(pixel >= dim))
        return;

    const uint spp = PATH_TRACER_SAMPLES_PER_PIXEL;
    float3 L = float3(0);
    for (uint i = 0; i < spp; i++)
    {
        RNG rng = RNG(pixel, frame * spp + i);
        float2 uv = float2(pixel + rng.next2D()) / float2(dim);
        uv.y = 1 - uv.y;
        Ray ray = g_scene.camera.get_ray(uv);
        Path path = Path(pixel, ray, rng);
        tracePath(path);
        L += path.L;
    }
    L /= spp;
    output[pixel] = float4(L, 1);
}

#endif // USE_RAYTRACING_PIPELINE


[[shader("compute")]]
[[numthreads(8, 8, 1)]]
void accumulate(
    uint3 tid: SV_DispatchThreadID,
    Texture2D<float4> input,
    RWTexture2D<float4> output,
    RWTexture2D<float4> accumulator,
    uniform uint reset
)
{
    uint2 pixel = tid.xy;
    uint2 dims;
    input.GetDimensions(dims.x, dims.y);
    if (any(pixel >= dims))
        return;
    float4 i = input[pixel];
    float4 a = float4(0.0);
    if (reset == 0)
        a = accumulator[pixel];
    a += float4(i.xyz, 1.0);
    float4 o = float4(a.xyz / a.w, 1.0);
    accumulator[pixel] = a;
    output[pixel] = o;
}

float3 acesFilm(float3 x)
{
    x *= 0.6;
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

[[shader("compute")]]
[[numthreads(8, 8, 1)]]
void tonemap(uint3 tid: SV_DispatchThreadID, Texture2D<float4> input, RWTexture2D<float4> output)
{
    uint2 pixel = tid.xy;
    uint2 dims;
    input.GetDimensions(dims.x, dims.y);
    if (any(pixel >= dims))
        return;
    float3 i = input[pixel].xyz;
    float3 o = acesFilm(i);
    output[pixel] = float4(o, 1.0);
}
