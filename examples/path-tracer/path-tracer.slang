#ifndef USE_RAYTRACING_PIPELINE
#warning "Missing USE_RAYTRACING_PIPELINE define"
#define USE_RAYTRACING_PIPELINE 0
#endif

static const float PI = 3.14159265358979323846;
static const float TWO_PI = 6.28318530717958647692;
static const float INV_PI = 0.31830988618379067154;


struct Ray
{
    float3 origin;
    float minT;
    float3 dir;
    float maxT;

    __init(float3 origin, float3 dir, float minT = 0.f, float maxT = 1000.0)
    {
        this.origin = origin;
        this.dir = dir;
        this.minT = minT;
        this.maxT = maxT;
    }

    RayDesc toRayDesc() { return { origin, minT, dir, maxT }; }
};

struct Frame
{
    float3 n;
    float3 t;
    float3 b;

    __init(float3 n)
    {
        // from Spencer, Jones "Into the Blue", eq(3)
        this.n = n;
        t = normalize(cross(n, float3(-n.z, n.x, -n.y)));
        b = cross(n, t);
    }

    float3 toLocal(float3 w) { return float3(dot(t, w), dot(b, w), dot(n, w)); }
    float3 toGlobal(float3 w) { return w.x * t + w.y * b + w.z * n; }
};

struct RNG
{
    uint state;

    __init(uint seed) { state = seed; }

    __init(uint2 pixel, uint sample_index)
    {
        state = (pixel.x * 100591) ^ (pixel.y * 102191) ^ (sample_index * 103643);
    }

    [mutating]
    uint next()
    {
        const uint A = 1664525u;
        const uint C = 1013904223u;
        state = (A * state + C);
        return state;
    }

    [mutating]
    float next1D() { return (next() >> 8) * 0x1p-24; }

    [mutating]
    float2 next2D()
    {
        float2 result;
        result.x = next1D();
        result.y = next1D();
        return result;
    }
};


struct Camera
{
    float3 position;
    float3 imageU;
    float3 imageV;
    float3 imageW;

    Ray get_ray(float2 uv)
    {
        uv = uv * 2 - 1;
        float3 dir = normalize(uv.x * imageU + uv.y * imageV + imageW);
        return Ray(position, dir);
    }
};

struct MaterialDesc
{
    float3 baseColor;
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

struct InstanceDesc
{
    uint meshID;
    uint materialID;
    uint transformID;
};

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

struct HitInfo
{
    bool valid;
    float2 bary;
    // float t;
    uint instanceID;
    uint primitiveIndex;
};

struct Path
{
    uint2 pixel;
    uint vertexIndex;
    Ray ray;
    float3 thp;
    float3 L;
    RNG rng;

    __init(uint2 pixel, Ray ray, RNG rng)
    {
        this.pixel = pixel;
        this.vertexIndex = 0;
        this.ray = ray;
        this.thp = float3(1);
        this.L = float3(0);
        this.rng = rng;
    }
};

float3 diffuseBrdfSample(float3 wi, float2 uv, out float pdf)
{
    float phi = uv.y * TWO_PI;
    float cos_theta = sqrt(1.f - uv.x);
    float sin_theta = sqrt(1.f - cos_theta * cos_theta);
    pdf = cos_theta * INV_PI;
    return float3(cos(phi) * sin_theta, sin(phi) * sin_theta, cos_theta);
}

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
    Vertex vertex = g_scene.getVertex(hit);
    // Flip normal if we hit the backside of the triangle
    vertex.normal *= sign(-dot(vertex.normal, path.ray.dir));
    Frame frame = Frame(vertex.normal);
    float3 wi = frame.toLocal(-path.ray.dir);
    float pdf;
    float3 wo = diffuseBrdfSample(wi, path.rng.next2D(), pdf);
    MaterialDesc material = g_scene.getMaterial(hit);
    float3 f = material.baseColor * INV_PI;
    path.thp *= f * abs(dot(wo, frame.n)) / pdf;
    path.vertexIndex++;
    path.ray = Ray(vertex.position + vertex.normal * 1e-6f, frame.toGlobal(wo));
    if (path.vertexIndex >= 5)
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

    const uint spp = 4;
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
            q.CommittedInstanceIndex(),
            q.CommittedPrimitiveIndex(),
        };
    }
    return hit;
}

void tracePath(inout Path path)
{
    while (path.vertexIndex < 5)
    {
        HitInfo hit = intersect(path.ray);
        if (!hit.valid)
        {
            path.L += path.thp * envmap(path.ray.dir);
            break;
        }
        Vertex vertex = g_scene.getVertex(hit);
        // Flip normal if we hit the backside of the triangle
        vertex.normal *= sign(-dot(vertex.normal, path.ray.dir));
        Frame frame = Frame(vertex.normal);
        float3 wi = frame.toLocal(-path.ray.dir);
        float pdf;
        float3 wo = diffuseBrdfSample(wi, path.rng.next2D(), pdf);
        MaterialDesc material = g_scene.getMaterial(hit);
        float3 f = material.baseColor * INV_PI;
        path.thp *= f * abs(dot(wo, frame.n)) / pdf;
        path.vertexIndex++;
        path.ray = Ray(vertex.position + vertex.normal * 1e-6f, frame.toGlobal(wo));
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

    const uint spp = 4;
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
