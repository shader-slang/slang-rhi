struct Frame
{
    row_major float4x4 viewProjection;
    row_major float4x4 lightViewProjection;
    float3 eye;
    float3 lightDirection;
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
Texture2D<float> shadowMap;
SamplerComparisonState shadowSampler;

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
    return result;
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

[shader("fragment")] float4 sceneFragment(Varyings input)
    : SV_Target
{
    const float pi = 3.14159265;
    float3 baseColor = material.baseColor;
    if (material.groundPattern != 0)
        baseColor *= lerp(0.82, 1.08, groundChecker(input.worldPosition.xz));
    float3 n = normalize(input.normal);
    float3 v = normalize(frame.eye - input.worldPosition);
    float3 l = frame.lightDirection;
    float3 h = normalize(v + l);
    float nv = max(dot(n, v), 0.001);
    float nl = saturate(dot(n, l));
    float nh = saturate(dot(n, h));
    float vh = saturate(dot(v, h));
    float roughness = clamp(material.roughness, 0.08, 1);
    float alpha = roughness * roughness;
    float alpha2 = alpha * alpha;
    float denominator = nh * nh * (alpha2 - 1) + 1;
    float distribution = alpha2 / max(pi * denominator * denominator, 1e-6);
    float k = (roughness + 1) * (roughness + 1) / 8;
    float geometry = nv / (nv * (1 - k) + k) * nl / (nl * (1 - k) + k);
    float3 f0 = lerp(float3(0.04), baseColor, material.metallic);
    float3 fresnel = f0 + (1 - f0) * pow(1 - vh, 5);
    float3 specular = distribution * geometry * fresnel / max(4 * nv * nl, 0.001);
    float3 diffuse = (1 - fresnel) * (1 - material.metallic) * baseColor / pi;
    float visibility = shadowVisibility(input.worldPosition, nl);
    float3 direct = (diffuse + specular) * float3(5.5, 5.1, 4.7) * nl * visibility;
    // A small hemispherical ambient term keeps the unlit side readable without
    // introducing environment maps or an image-based-lighting preprocessing path.
    float3 ambient = lerp(float3(0.10, 0.09, 0.08), float3(0.42, 0.50, 0.65), n.y * 0.5 + 0.5);
    float3 color = direct + ambient * baseColor * (1 - 0.6 * material.metallic);
    return float4(color, 1);
}
