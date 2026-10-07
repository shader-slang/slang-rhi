Texture2D<float4> source;
SamplerState linearSampler;
[format("rgba16f")] WTexture2D<float4> destination;
uint bloomPass;

[shader("compute")][numthreads(8, 8, 1)] void bloomMain(uint3 tid : SV_DispatchThreadID)
{
    uint width, height;
    destination.GetDimensions(width, height);
    if (any(tid.xy >= uint2(width, height)))
        return;
    float2 uv = (float2(tid.xy) + 0.5) / float2(width, height);
    float3 color = 0;
    if (bloomPass == 0)
    {
        uint sourceWidth, sourceHeight;
        source.GetDimensions(sourceWidth, sourceHeight);
        // Downsample and extract highlights with a soft threshold.
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 2; ++x)
            {
                float2 offset = (float2(x, y) - 0.5) / float2(sourceWidth, sourceHeight);
                float3 sample = source.SampleLevel(linearSampler, uv + offset, 0).rgb;
                float brightness = max(sample.r, max(sample.g, sample.b));
                float soft = clamp(brightness - 0.5, 0, 1);
                float contribution = max(brightness - 1, soft * soft * 0.5);
                color += sample * contribution / max(brightness, 1e-5) * 0.25;
            }
    }
    else
    {
        const float weights[5] = {0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216};
        float2 direction = bloomPass == 1 ? float2(1.5 / width, 0) : float2(0, 1.5 / height);
        color = source.SampleLevel(linearSampler, uv, 0).rgb * weights[0];
        for (int i = 1; i <= 4; ++i)
        {
            color += source.SampleLevel(linearSampler, uv + i * direction, 0).rgb * weights[i];
            color += source.SampleLevel(linearSampler, uv - i * direction, 0).rgb * weights[i];
        }
    }
    destination.Store(tid.xy, float4(color, 1));
}

Texture2D<float4> bloom;
float exposure;
float bloomStrength;
uint encodeSrgb;

struct FullscreenVertex
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD;
};

[shader("vertex")] FullscreenVertex fullscreenVertex(uint vertexID : SV_VertexID)
{
    FullscreenVertex result;
    result.uv = float2((vertexID << 1) & 2, vertexID & 2);
    result.position = float4(result.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return result;
}

float3 linearToSrgb(float3 color)
{
    return select(color <= 0.0031308, color * 12.92, 1.055 * pow(color, 1.0 / 2.4) - 0.055);
}

[shader("fragment")] float4 toneMapFragment(FullscreenVertex input)
    : SV_Target
{
    float3 color = source.SampleLevel(linearSampler, input.uv, 0).rgb;
    color += bloomStrength * bloom.SampleLevel(linearSampler, input.uv, 0).rgb;
    color *= exp2(exposure);
    // ACES-inspired fitted curve. All lighting and bloom above operate in linear HDR.
    color = saturate((color * (2.51 * color + 0.03)) / (color * (2.43 * color + 0.59) + 0.14));
    if (encodeSrgb != 0)
        color = linearToSrgb(color);
    return float4(color, 1);
}
