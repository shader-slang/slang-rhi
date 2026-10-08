#include "environment-common.slang"

static const float pi = 3.14159265;
static const uint sampleCount = 256;

float2 hammersley(uint i)
{
    return float2(float(i) / sampleCount, float(reversebits(i)) * 2.3283064365386963e-10);
}

float3 tangentToWorld(float3 local, float3 n)
{
    float3 up = abs(n.y) < 0.999 ? float3(0, 1, 0) : float3(0, 0, 1);
    float3 tangent = normalize(cross(up, n));
    return tangent * local.x + cross(n, tangent) * local.y + n * local.z;
}

float3 sampleGgx(float2 xi, float roughness)
{
    float alpha = roughness * roughness;
    float cosTheta = sqrt((1 - xi.y) / (1 + (alpha * alpha - 1) * xi.y));
    float sinTheta = sqrt(max(0, 1 - cosTheta * cosTheta));
    float phi = 2 * pi * xi.x;
    return float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
}

[format("rgba16f")] WTexture2DArray<float4> environmentOutput;
uint environmentPreset;
uint diffuseFilter;
float filterRoughness;

[shader("compute")][numthreads(8, 8, 1)] void environmentMain(uint3 tid : SV_DispatchThreadID)
{
    uint width, height, layers;
    environmentOutput.GetDimensions(width, height, layers);
    if (any(tid >= uint3(width, height, layers)))
        return;
    float3 n = environmentFaceDirection(tid.z, (float2(tid.xy) + 0.5) / float2(width, height));
    float3 color = 0;
    float weight = 0;
    if (diffuseFilter == 0 && filterRoughness == 0)
        color = evaluateEnvironment(n, environmentPreset);
    else
    {
        for (uint i = 0; i < sampleCount; ++i)
        {
            float2 xi = hammersley(i);
            float3 l;
            if (diffuseFilter != 0)
            {
                // Cosine-weighted sampling stores irradiance / pi.
                float phi = 2 * pi * xi.x;
                l = tangentToWorld(float3(sqrt(xi.y) * float2(cos(phi), sin(phi)), sqrt(1 - xi.y)), n);
            }
            else
            {
                float3 h = tangentToWorld(sampleGgx(xi, filterRoughness), n);
                l = reflect(-n, h);
            }
            float w = diffuseFilter != 0 ? 1 : saturate(dot(n, l));
            color += evaluateEnvironment(l, environmentPreset) * w;
            weight += w;
        }
        color /= max(weight, 0.001);
    }
    environmentOutput.Store(tid, float4(color, 1));
}

[format("rgba16f")] WTexture2D<float4> brdfOutput;

float smithG1(float cosine, float alphaSquared)
{
    return 2 * cosine / max(cosine + sqrt(alphaSquared + (1 - alphaSquared) * cosine * cosine), 1e-6);
}

[shader("compute")][numthreads(8, 8, 1)] void brdfMain(uint3 tid : SV_DispatchThreadID)
{
    uint width, height;
    brdfOutput.GetDimensions(width, height);
    if (any(tid.xy >= uint2(width, height)))
        return;
    float nv = (tid.x + 0.5) / width;
    float roughness = (tid.y + 0.5) / height;
    float3 v = float3(sqrt(1 - nv * nv), 0, nv);
    float alphaSquared = pow(roughness, 4);
    float2 integral = 0;
    for (uint i = 0; i < sampleCount; ++i)
    {
        float3 h = sampleGgx(hammersley(i), roughness);
        float3 l = reflect(-v, h);
        float nl = saturate(l.z);
        float vh = saturate(dot(v, h));
        if (nl > 0)
        {
            float visibility = smithG1(nv, alphaSquared) * smithG1(nl, alphaSquared) * vh / max(h.z * nv, 1e-6);
            float fresnel = pow(1 - vh, 5);
            integral += float2(1 - fresnel, fresnel) * visibility;
        }
    }
    brdfOutput.Store(tid.xy, float4(integral / sampleCount, 0, 1));
}
