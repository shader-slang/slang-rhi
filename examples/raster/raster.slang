#include "environment-common.slang"

struct Frame
{
    row_major float4x4 viewProjection;
    row_major float4x4 lightViewProjection;
    float3 eye;
    float3 lightDirection;
    float3 cameraRight;
    float3 cameraUp;
    float3 cameraForward;
    float environmentRotation;
    uint environmentPreset;
    float environmentIntensity;
};

struct Material
{
    float3 baseColor;
    float metallic;
    float roughness;
    uint groundPattern;
};

ConstantBuffer<Frame> frame;
ConstantBuffer<Material> material;
DepthTexture2D shadowMap;
SamplerComparisonState shadowSampler;
TextureCube<float4> specularEnvironment;
TextureCube<float4> diffuseEnvironment;
Texture2D<float4> environmentBrdf;
SamplerState environmentSampler;

struct SphereInstance
{
    float4 translationScale;
    float4 baseColorMetallic;
    float4 materialParameters;
};
StructuredBuffer<SphereInstance> sphereInstances;

struct BackgroundVertex
{
    float4 position : SV_Position;
    float2 ndc : TEXCOORD;
};

[shader("vertex")] BackgroundVertex backgroundVertex(uint vertexID : SV_VertexID)
{
    BackgroundVertex result;
    float2 uv = float2((vertexID << 1) & 2, vertexID & 2);
    result.ndc = uv * float2(2, -2) + float2(-1, 1);
    result.position = float4(result.ndc, 1, 1);
    return result;
}

    [shader("fragment")] float4 backgroundFragment(BackgroundVertex input)
    : SV_Target
{
    float3 direction = normalize(frame.cameraForward + input.ndc.x * frame.cameraRight + input.ndc.y * frame.cameraUp);
    float3 color =
        evaluateEnvironment(rotateEnvironment(direction, frame.environmentRotation), frame.environmentPreset);
    if (frame.environmentPreset != 0)
    {
        float cosine = dot(direction, frame.lightDirection);
        // A slightly enlarged, antialiased sun disk. Its lighting is supplied
        // exclusively by the existing shadowed directional light.
        float edge = max(fwidth(cosine), 1e-6);
        color += float3(5.5, 5.1, 4.7) * smoothstep(0.9998 - edge, 0.9998 + edge, cosine);
    }
    return float4(color, 1);
}

struct Vertex
{
    float3 position : POSITION;
    float3 normal : NORMAL;
};

struct Varyings
{
    float4 position : SV_Position;
    float3 worldPosition : WORLD_POSITION;
    float3 normal : NORMAL;
    float4 baseColorMetallic : COLOR0;
    float2 roughnessGround : TEXCOORD0;
};

[shader("vertex")] float4 shadowVertex(float3 position : POSITION)
    : SV_Position
{
    return mul(frame.lightViewProjection, float4(position, 1));
}

// D3D11 currently requires a fragment entry point even for depth-only pipelines.
[shader("fragment")] void shadowFragment()
{
}

    [shader("vertex")] Varyings sceneVertex(Vertex vertex)
{
    Varyings result;
    result.position = mul(frame.viewProjection, float4(vertex.position, 1));
    result.worldPosition = vertex.position;
    result.normal = vertex.normal;
    result.baseColorMetallic = float4(material.baseColor, material.metallic);
    result.roughnessGround = float2(material.roughness, material.groundPattern);
    return result;
}

[shader("vertex")] Varyings sphereVertex(Vertex vertex, uint instanceID : SV_InstanceID)
{
    SphereInstance instance = sphereInstances[instanceID];
    Varyings result;
    result.worldPosition = vertex.position * instance.translationScale.w + instance.translationScale.xyz;
    result.position = mul(frame.viewProjection, float4(result.worldPosition, 1));
    result.normal = vertex.normal;
    // These attributes have identical values at every vertex of an instance.
    result.baseColorMetallic = instance.baseColorMetallic;
    result.roughnessGround = float2(instance.materialParameters.x, 0);
    return result;
}

[shader("vertex")] float4 sphereShadowVertex(float3 position : POSITION, uint instanceID : SV_InstanceID) : SV_Position
{
    float4 transform = sphereInstances[instanceID].translationScale;
    return mul(frame.lightViewProjection, float4(position * transform.w + transform.xyz, 1));
}

float shadowVisibility(float3 position, float nDotL)
{
    float4 clip = mul(frame.lightViewProjection, float4(position, 1));
    float3 ndc = clip.xyz / clip.w;
    float2 uv = ndc.xy * float2(0.5, -0.5) + 0.5;
    if (any(uv < 0) || any(uv > 1) || ndc.z <= 0 || ndc.z >= 1)
        return 1;
    uint width, height;
    shadowMap.GetDimensions(width, height);
    float bias = max(0.0003, 0.0012 * (1 - nDotL));
    float visibility = 0;
    [unroll] for (int y = -1; y <= 1; ++y)[unroll] for (int x = -1; x <= 1; ++x) visibility +=
        shadowMap.SampleCmpLevelZero(shadowSampler, uv + float2(x, y) / float2(width, height), ndc.z - bias);
    return visibility / 9;
}

// Integral of a unit-width alternating stripe. Integrating over the pixel's
// footprint makes the checker converge to its average instead of shimmering.
float2 integratedStripe(float2 p)
{
    return 1 - abs(2 * frac(p * 0.5) - 1);
}

float groundChecker(float2 p)
{
    float2 footprint = max(abs(ddx(p)) + abs(ddy(p)), float2(0.0001));
    float2 stripe = (integratedStripe(p + footprint * 0.5) - integratedStripe(p - footprint * 0.5)) / footprint;
    return 0.5 + 0.5 * stripe.x * stripe.y;
}

float4 shadeSurface(Varyings input, Material surface)
{
    const float pi = 3.14159265;
    float3 baseColor = surface.baseColor;
    // Evaluate derivatives unconditionally: material attributes can vary between
    // fragments, and WebGPU requires derivatives in uniform control flow.
    float checkerScale = lerp(0.82, 1.08, groundChecker(input.worldPosition.xz));
    baseColor *= 1 + float(surface.groundPattern) * (checkerScale - 1);
    float3 n = normalize(input.normal);
    float3 v = normalize(frame.eye - input.worldPosition);
    float3 l = frame.lightDirection;
    float3 h = normalize(v + l);
    float nv = max(dot(n, v), 0.001);
    float nl = saturate(dot(n, l));
    float nh = saturate(dot(n, h));
    float vh = saturate(dot(v, h));
    float roughness = clamp(surface.roughness, 0.08, 1);
    float alpha = roughness * roughness;
    float alpha2 = alpha * alpha;
    float denominator = nh * nh * (alpha2 - 1) + 1;
    float distribution = alpha2 / max(pi * denominator * denominator, 1e-6);
    float k = (roughness + 1) * (roughness + 1) / 8;
    float geometry = nv / (nv * (1 - k) + k) * nl / (nl * (1 - k) + k);
    float3 f0 = lerp(float3(0.04), baseColor, surface.metallic);
    float3 fresnel = f0 + (1 - f0) * pow(1 - vh, 5);
    float3 specular = distribution * geometry * fresnel / max(4 * nv * nl, 0.001);
    float3 diffuse = (1 - fresnel) * (1 - surface.metallic) * baseColor / pi;
    float visibility = shadowVisibility(input.worldPosition, nl);
    float3 direct = (diffuse + specular) * float3(5.5, 5.1, 4.7) * nl * visibility;
    float3 environmentNormal = rotateEnvironment(n, frame.environmentRotation);
    float3 environmentReflection = rotateEnvironment(reflect(-v, n), frame.environmentRotation);
    // The preprocessing stores nine levels spanning roughness [0, 1].
    float3 radiance = specularEnvironment.SampleLevel(environmentSampler, environmentReflection, roughness * 8).rgb;
    float3 irradiance = diffuseEnvironment.SampleLevel(environmentSampler, environmentNormal, 0).rgb;
    float2 brdf = environmentBrdf.SampleLevel(environmentSampler, float2(nv, roughness), 0).rg;
    float3 environmentFresnel = f0 + (max(float3(1 - roughness), f0) - f0) * pow(1 - nv, 5);
    float3 indirectDiffuse = irradiance * baseColor * (1 - surface.metallic) * (1 - environmentFresnel);
    float3 indirectSpecular = radiance * (f0 * brdf.x + brdf.y);
    float3 color = direct + frame.environmentIntensity * (indirectDiffuse + indirectSpecular);
    return float4(color, 1);
}

[shader("fragment")] float4 sceneFragment(Varyings input) : SV_Target
{
    Material surface;
    surface.baseColor = input.baseColorMetallic.rgb;
    surface.metallic = input.baseColorMetallic.w;
    surface.roughness = input.roughnessGround.x;
    surface.groundPattern = input.roughnessGround.y >= 0.5 ? 1 : 0;
    return shadeSurface(input, surface);
}
