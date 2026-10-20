Texture2D<float4> preview;
Texture2D<float> fontAtlas;
StructuredBuffer<uint> labels;
SamplerState linearSampler;
[format("rgba32f")] WTexture2D<float4> output;
uint2 outputSize;
float panelSize;
float panelTop;
float fontScale;

float3 heatmap(float error)
{
    float t = saturate(error / 0.25f);
    return lerp(lerp(float3(0.003f, 0.006f, 0.015f), float3(0.08f, 0.35f, 0.7f), saturate(t * 2)),
                float3(1.0f, 0.35f, 0.04f), saturate(t * 2 - 1));
}

float text(float2 pixel, float2 origin, uint offset, uint length)
{
    float2 p = (pixel - origin) / fontScale;
    if (any(p < 0) || p.y >= 9 || p.x >= length * 6) return 0;
    uint2 local = uint2(p);
    uint glyph = labels[offset + local.x / 6] - 32;
    return fontAtlas.Load(int3(uint2(glyph % 16, glyph / 16) * uint2(6, 9) + uint2(local.x % 6, local.y), 0));
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
    float2 labelOrigin = float2(left, panelTop - 18 * fontScale);
    float glyph = text(pixel, labelOrigin, panel * 16, 16);
    color = lerp(color, float3(0.78f, 0.84f, 0.91f), glyph);
    if (panel == 2)
    {
        float2 bar = pixel - float2(left, panelTop + panelSize + 10);
        if (bar.x >= 0 && bar.x < panelSize && bar.y >= 0 && bar.y < 8)
            color = heatmap(0.25f * bar.x / panelSize);
        glyph = text(pixel, float2(left, panelTop + panelSize + 24), 48, 1);
        glyph += text(pixel, float2(left + panelSize - 30 * fontScale, panelTop + panelSize + 24), 59, 5);
        color = lerp(color, float3(0.65f, 0.73f, 0.83f), glyph);
    }
    output.Store(tid.xy, float4(color, 1));
}
