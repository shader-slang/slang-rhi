Texture2D<float4> preview;
SamplerState linearSampler;
[format("rgba32f")] WTexture2D<float4> output;
uint2 outputSize;
float panelSize;
float panelTop;

float3 heatmap(float error)
{
    float t = saturate(error / 0.25f);
    return lerp(lerp(float3(0.003f, 0.006f, 0.015f), float3(0.08f, 0.35f, 0.7f), saturate(t * 2)),
                float3(1.0f, 0.35f, 0.04f), saturate(t * 2 - 1));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void compose(uint3 tid : SV_DispatchThreadID)
{
    if (any(tid.xy >= outputSize)) return;
    float2 pixel = float2(tid.xy) + 0.5f;
    float columnWidth = float(outputSize.x) / 3.0f;
    uint panel = min(uint(pixel.x / columnWidth), 2u);
    float left = columnWidth * (float(panel) + 0.5f) - panelSize * 0.5f;
    float2 uv = (pixel - float2(left, panelTop)) / panelSize;
    float3 color = float3(0.008f, 0.012f, 0.022f);
    if (all(uv >= 0) && all(uv < 1))
    {
        uint width, height;
        preview.GetDimensions(width, height);
        // Keep bilinear footprints inside their panel, including at the edges.
        float2 local = clamp(uv, 0.5f / height, 1.0f - 0.5f / height);
        float3 value = preview.SampleLevel(linearSampler, float2((panel + local.x) / 3.0f, local.y), 0).rgb;
        color = panel == 2 ? heatmap(value.x) : value;
    }
    if (panel == 2)
    {
        float2 bar = pixel - float2(left, panelTop + panelSize + 10);
        if (bar.x >= 0 && bar.x < panelSize && bar.y >= 0 && bar.y < 8)
            color = heatmap(0.25f * bar.x / panelSize);
    }
    output.Store(tid.xy, float4(color, 1));
}
