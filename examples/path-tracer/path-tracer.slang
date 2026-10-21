#include "settings.h"

static const float TWO_PI = 6.28318530717958647692;

struct RNG
{
    uint state;

    __init(uint2 pixel, uint sampleIndex)
    {
        // Mix pixel/sample IDs before starting the sequence to avoid correlated neighboring pixels.
        state = (pixel.x * 100591) ^ (pixel.y * 102191) ^ (sampleIndex * 103643);
        state ^= state >> 16;
        state *= 0x7feb352d;
        state ^= state >> 15;
        state *= 0x846ca68b;
        state ^= state >> 16;
    }

    [mutating]
    float next1D()
    {
        state = 1664525u * state + 1013904223u;
        return (state >> 8) * 0x1p-24;
    }

    [mutating]
    float2 next2D()
    {
        float x = next1D();
        float y = next1D();
        return float2(x, y);
    }
};

struct Camera
{
    float3 position;
    float3 imageU;
    float3 imageV;
    float3 imageW;

    RayDesc generateRay(float2 uv)
    {
        uv = uv * 2 - 1;
        return {position, 0, normalize(uv.x * imageU + uv.y * imageV + imageW), 1000};
    }
};

// Keep these structured-buffer layouts in sync with the C++ scene records.
struct Material
{
    float3 baseColor;
    uint mirror;
};

struct Instance
{
    row_major float4x4 transform;
    row_major float4x4 normalTransform;
    uint meshID;
    uint materialID;
    uint2 padding;
};

struct Vertex
{
    float3 position;
    float3 normal;
    float2 uv;
};

struct MeshDesc
{
    uint vertexCount;
    uint indexCount;
    uint vertexOffset;
    uint indexOffset;
};

// A compact hit record shared by ray pipelines and ray queries.
struct HitInfo
{
    float2 bary;
    uint instanceID;
    uint primitiveIndex;

    bool valid() { return instanceID != 0xffffffff; }
};

[raypayload]
struct RayPayload
{
    HitInfo hit : read(caller) : write(caller, closesthit, miss);
};

struct Surface
{
    float3 position;
    float3 normal;
    Material material;
};

float3 sampleCosineHemisphere(float3 normal, float2 uv)
{
    float phi = uv.y * TWO_PI;
    float radius = sqrt(uv.x);
    float3 local = float3(cos(phi) * radius, sin(phi) * radius, sqrt(1 - uv.x));
    // Choose an axis away from the normal so the cross product cannot vanish.
    float3 axis = abs(normal.z) < 0.999f ? float3(0, 0, 1) : float3(0, 1, 0);
    float3 tangent = normalize(cross(axis, normal));
    float3 bitangent = cross(normal, tangent);
    return local.x * tangent + local.y * bitangent + local.z * normal;
}

struct Scene
{
    RaytracingAccelerationStructure tlas;
    StructuredBuffer<Material> materials;
    StructuredBuffer<MeshDesc> meshes;
    StructuredBuffer<Instance> instances;
    StructuredBuffer<Vertex> vertices;
    StructuredBuffer<uint> indices;

    Surface getSurface(HitInfo hit)
    {
        Instance instance = instances[hit.instanceID];
        MeshDesc mesh = meshes[instance.meshID];
        uint firstIndex = mesh.indexOffset + hit.primitiveIndex * 3;
        Vertex a = vertices[mesh.vertexOffset + indices[firstIndex]];
        Vertex b = vertices[mesh.vertexOffset + indices[firstIndex + 1]];
        Vertex c = vertices[mesh.vertexOffset + indices[firstIndex + 2]];
        float3 weights = float3(1 - hit.bary.x - hit.bary.y, hit.bary);
        float3 position = weights.x * a.position + weights.y * b.position + weights.z * c.position;
        float3 normal = weights.x * a.normal + weights.y * b.normal + weights.z * c.normal;
        return {
            mul(instance.transform, float4(position, 1)).xyz,
            normalize(mul(instance.normalTransform, float4(normal, 0)).xyz),
            materials[instance.materialID],
        };
    }
};

ParameterBlock<Scene> g_scene;

float3 environment(float3 direction)
{
    float3 sky = lerp(float3(0.9, 0.85, 0.7), float3(0.28, 0.48, 0.8), saturate(direction.y));
    sky = lerp(float3(0.12, 0.1, 0.08), sky, smoothstep(-0.2, 0.05, direction.y));
    // A broad warm light makes soft shadows without requiring explicit light sampling.
    float light = pow(saturate(dot(direction, normalize(float3(-0.6, 0.8, 0.4)))), 12);
    return sky + float3(3.5, 2.8, 1.9) * light;
}

interface ITracer
{
    HitInfo intersect(RayDesc ray);
};

// Only intersection differs between the two execution models. Slang specializes
// the shared integrator for each tracer; no dynamic dispatch is needed.
struct RayPipelineTracer : ITracer
{
    HitInfo intersect(RayDesc ray)
    {
        RayPayload payload = {{float2(0), 0xffffffff, 0}};
        TraceRay(g_scene.tlas, RAY_FLAG_FORCE_OPAQUE, 0xff, 0, 0, 0, ray, payload);
        return payload.hit;
    }
};

struct RayQueryTracer : ITracer
{
    HitInfo intersect(RayDesc ray)
    {
        HitInfo hit = {float2(0), 0xffffffff, 0};
        RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
        query.TraceRayInline(g_scene.tlas, 0, 0xff, ray);
        while (query.Proceed())
            ;
        if (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
            hit = {
                query.CommittedTriangleBarycentrics(),
                query.CommittedInstanceID(),
                query.CommittedPrimitiveIndex(),
            };
        return hit;
    }
};

// [shader("miss")]
// void mainMiss(inout RayPayload payload)
// {
//     payload.hit = {float2(0), 0xffffffff, 0};
// }

// [shader("closesthit")]
// void mainClosestHit(inout RayPayload payload, BuiltInTriangleIntersectionAttributes attributes)
// {
//     payload.hit = {attributes.barycentrics, InstanceID(), PrimitiveIndex()};
// }

float3 tracePath<T : ITracer>(T tracer, RayDesc ray, inout RNG rng, uint maxBounces)
{
    float3 throughput = float3(1);
    for (uint bounce = 0; bounce <= maxBounces; ++bounce)
    {
        HitInfo hit = tracer.intersect(ray);
        if (!hit.valid())
            return throughput * environment(ray.Direction);
        // After the last scatter, still allow an escaping ray to see the environment.
        if (bounce == maxBounces)
            break;
        Surface surface = g_scene.getSurface(hit);
        if (dot(surface.normal, ray.Direction) > 0)
            surface.normal = -surface.normal;
        float3 direction;
        if (surface.material.mirror != 0)
            direction = reflect(ray.Direction, surface.normal);
        else
            direction = sampleCosineHemisphere(surface.normal, rng.next2D());
        // Lambertian albedo/pi and cosine-weighted PDF cancel. Mirror reflectance
        // also simply scales throughput, so both materials use this update.
        throughput *= surface.material.baseColor;
        ray = {surface.position + surface.normal * 1e-4f, 0, direction, 1000};
    }
    return float3(0);
}

float4 renderPixel<T : ITracer>(T tracer, uint2 pixel, uint2 dimensions, Camera camera, uint frame, uint maxBounces)
{
    float3 radiance = float3(0);
    for (uint sample = 0; sample < PATH_TRACER_SAMPLES_PER_PIXEL; ++sample)
    {
        RNG rng = RNG(pixel, frame * PATH_TRACER_SAMPLES_PER_PIXEL + sample);
        float2 uv = (float2(pixel) + rng.next2D()) / float2(dimensions);
        uv.y = 1 - uv.y;
        radiance += tracePath(tracer, camera.generateRay(uv), rng, maxBounces);
    }
    return float4(radiance / PATH_TRACER_SAMPLES_PER_PIXEL, 1);
}

// [shader("raygeneration")]
// void renderRaygen(RWTexture2D<float4> output, uniform Camera camera, uniform uint frame, uniform uint maxBounces)
// {
//     uint2 pixel = DispatchRaysIndex().xy;
//     uint2 dimensions;
//     output.GetDimensions(dimensions.x, dimensions.y);
//     output[pixel] = renderPixel(RayPipelineTracer(), pixel, dimensions, camera, frame, maxBounces);
// }

[shader("compute")]
[numthreads(8, 8, 1)]
void renderCompute(
    uint3 tid : SV_DispatchThreadID,
    RWTexture2D<float4> output,
    uniform Camera camera,
    uniform uint frame,
    uniform uint maxBounces
)
{
    uint2 dimensions;
    output.GetDimensions(dimensions.x, dimensions.y);
    if (any(tid.xy >= dimensions))
        return;
    output[tid.xy] = renderPixel(RayQueryTracer(), tid.xy, dimensions, camera, frame, maxBounces);
}

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
