// TODO
#define USE_RAYTRACING_PIPELINE 0

#ifndef USE_RAYTRACING_PIPELINE
#error "Missing USE_RAYTRACING_PIPELINE define"
#endif

static const float PI = 3.14159265358979323846;
static const float TWO_PI = 6.28318530717958647692;
static const float INV_PI = 0.31830988618379067154;


struct Ray
{
    float3 origin;
    float t_min;
    float3 dir;
    float t_max;

    __init(float3 origin, float3 dir, float t_min = 0.f, float t_max = 1000.0)
    {
        this.origin = origin;
        this.dir = dir;
        this.t_min = t_min;
        this.t_max = t_max;
    }

    RayDesc to_ray_desc() { return { origin, t_min, dir, t_max }; }
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

    float3 to_local(float3 w) { return float3(dot(t, w), dot(b, w), dot(n, w)); }
    float3 to_global(float3 w) { return w.x * t + w.y * b + w.z * n; }
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
    float next_1d() { return (next() >> 8) * 0x1p-24; }

    [mutating]
    float2 next_2d()
    {
        float2 result;
        result.x = next_1d();
        result.y = next_1d();
        return result;
    }

    [mutating]
    float3 next_3d()
    {
        float3 result;
        result.x = next_1d();
        result.y = next_1d();
        result.z = next_1d();
        return result;
    }
};


struct Camera
{
    float3 position;
    float3 image_u;
    float3 image_v;
    float3 image_w;

    Ray get_ray(float2 uv)
    {
        uv = uv * 2 - 1;
        float3 dir = normalize(uv.x * image_u + uv.y * image_v + image_w);
        return Ray(position, dir);
    }
};

struct MaterialDesc
{
    float3 base_color;
};

struct Vertex
{
    float3 position;
    float3 normal;
    float2 uv;
};

struct MeshDesc
{
    uint vertex_count;
    uint index_count;
    uint vertex_offset;
    uint index_offset;
};

struct InstanceDesc
{
    uint mesh_id;
    uint material_id;
    uint transform_id;
};

struct Scene
{
    RaytracingAccelerationStructure tlas;

    StructuredBuffer<MaterialDesc> material_descs;
    StructuredBuffer<MeshDesc> mesh_descs;
    StructuredBuffer<InstanceDesc> instance_descs;
    StructuredBuffer<Vertex> vertices;
    StructuredBuffer<uint> indices;
    StructuredBuffer<float4x4> transforms;
    StructuredBuffer<float4x4> inverse_transpose_transforms;

    Camera camera;

    MaterialDesc get_material(HitInfo hit_info)
    {
        InstanceDesc instance_desc = g_scene.instance_descs[hit_info.instance_id];
        return g_scene.material_descs[instance_desc.material_id];
    }

    Vertex get_vertex(HitInfo hit_info)
    {
        return get_vertex(hit_info.instance_id, hit_info.primitive_index, hit_info.bary);
    }

    Vertex get_vertex(uint instance_id, uint primitive_index, float2 bary)
    {
        InstanceDesc instance_desc = g_scene.instance_descs[instance_id];
        MeshDesc mesh_desc = g_scene.mesh_descs[instance_desc.mesh_id];
        float4x4 transform = g_scene.transforms[instance_desc.transform_id];
        float4x4 inverse_transpose_transform = g_scene.inverse_transpose_transforms[instance_desc.transform_id];
        uint i0 = g_scene.indices[mesh_desc.index_offset + primitive_index * 3 + 0];
        uint i1 = g_scene.indices[mesh_desc.index_offset + primitive_index * 3 + 1];
        uint i2 = g_scene.indices[mesh_desc.index_offset + primitive_index * 3 + 2];
        Vertex v0 = g_scene.vertices[mesh_desc.vertex_offset + i0];
        Vertex v1 = g_scene.vertices[mesh_desc.vertex_offset + i1];
        Vertex v2 = g_scene.vertices[mesh_desc.vertex_offset + i2];
        float3 b = float3(1.0 - bary.x - bary.y, bary.x, bary.y);
        return {
            mul(transform, float4(b.x * v0.position + b.y * v1.position + b.z * v2.position, 1.0)).xyz,
            normalize(
                mul(inverse_transpose_transform, float4(b.x * v0.normal + b.y * v1.normal + b.z * v2.normal, 0)).xyz
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
    uint instance_id;
    uint primitive_index;
};

struct Path
{
    uint2 pixel;
    uint vertex_index;
    Ray ray;
    float3 thp;
    float3 L;
    RNG rng;

    __init(uint2 pixel, Ray ray, RNG rng)
    {
        this.pixel = pixel;
        this.vertex_index = 0;
        this.ray = ray;
        this.thp = float3(1);
        this.L = float3(0);
        this.rng = rng;
    }
};

float3 diffuse_brdf_sample(float3 wi, float2 uv, out float pdf)
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
void rt_miss(inout Path path)
{
    path.L += path.thp * envmap(path.ray.dir);
}

[shader("closesthit")]
void rt_closest_hit(inout Path path, BuiltInTriangleIntersectionAttributes attribs)
{
    HitInfo hit = {
        true,
        attribs.barycentrics,
        // RayTMin(),
        InstanceID(),
        PrimitiveIndex(),
    };
    Vertex vertex = g_scene.get_vertex(hit);
    // Flip normal if we hit the backside of the triangle
    vertex.normal *= sign(-dot(vertex.normal, path.ray.dir));
    Frame frame = Frame(vertex.normal);
    float3 wi = frame.to_local(-path.ray.dir);
    float pdf;
    float3 wo = diffuse_brdf_sample(wi, path.rng.next_2d(), pdf);
    MaterialDesc material = g_scene.get_material(hit);
    float3 f = material.base_color * INV_PI;
    path.thp *= f * abs(dot(wo, frame.n)) / pdf;
    path.vertex_index++;
    path.ray = Ray(vertex.position + vertex.normal * 1e-6f, frame.to_global(wo));
    if (path.vertex_index >= 5)
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
        path.ray.to_ray_desc(),
        path
    );
}

[shader("raygeneration")]
void rt_ray_gen()
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
        float2 uv = float2(pixel + rng.next_2d()) / float2(dim);
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
            path.ray.to_ray_desc(),
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
    q.TraceRayInline(g_scene.tlas, 0, 0xff, ray.to_ray_desc());
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

void trace_path(inout Path path)
{
    while (path.vertex_index < 5)
    {
        HitInfo hit = intersect(path.ray);
        if (!hit.valid)
        {
            path.L += path.thp * envmap(path.ray.dir);
            break;
        }
        Vertex vertex = g_scene.get_vertex(hit);
        // Flip normal if we hit the backside of the triangle
        vertex.normal *= sign(-dot(vertex.normal, path.ray.dir));
        Frame frame = Frame(vertex.normal);
        float3 wi = frame.to_local(-path.ray.dir);
        float pdf;
        float3 wo = diffuse_brdf_sample(wi, path.rng.next_2d(), pdf);
        MaterialDesc material = g_scene.get_material(hit);
        float3 f = material.base_color * INV_PI;
        path.thp *= f * abs(dot(wo, frame.n)) / pdf;
        path.vertex_index++;
        path.ray = Ray(vertex.position + vertex.normal * 1e-6f, frame.to_global(wo));
    }
}

[shader("compute")]
[numthreads(8, 8, 1)]
void mainCompute(uint3 tid: SV_DispatchThreadID, RWTexture2D<float4> output, uniform uint frame)
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
        float2 uv = float2(pixel + rng.next_2d()) / float2(dim);
        uv.y = 1 - uv.y;
        Ray ray = g_scene.camera.get_ray(uv);
        Path path = Path(pixel, ray, rng);
        trace_path(path);
        L += path.L;
    }
    L /= spp;
    output[pixel] = float4(L, 1);
}

#endif // USE_RAYTRACING_PIPELINE
