#pragma once

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
        this.n = n;
        // Choose an axis away from the normal so the cross product cannot vanish.
        float3 axis = abs(n.z) < 0.999f ? float3(0, 0, 1) : float3(0, 1, 0);
        t = normalize(cross(axis, n));
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

float3 sampleCosineHemisphere(float2 uv)
{
    float phi = uv.y * TWO_PI;
    float cos_theta = sqrt(1.f - uv.x);
    float sin_theta = sqrt(1.f - cos_theta * cos_theta);
    return float3(cos(phi) * sin_theta, sin(phi) * sin_theta, cos_theta);
}

void scatter(inout Path path, Vertex vertex, MaterialDesc material)
{
    // Orient the normal toward the incident ray. A grazing ray must not zero it.
    if (dot(vertex.normal, path.ray.dir) > 0)
        vertex.normal = -vertex.normal;
    Frame frame = Frame(vertex.normal);
    float3 wo = sampleCosineHemisphere(path.rng.next2D());
    // Lambertian f = albedo/pi and cosine-weighted pdf = wo.z/pi cancel.
    // wo is local to the frame; dot(wo, a world-space normal) would be incorrect.
    path.thp *= material.baseColor;
    path.vertexIndex++;
    path.ray = Ray(vertex.position + vertex.normal * 1e-6f, frame.toGlobal(wo));
}

