Texture2D<float> fontAtlas;
StructuredBuffer<uint> characters;
uint2 gridSize;
uint2 glyphSize;
float2 panelOrigin;
float2 panelEnd;
float2 textOrigin;
float fontScale;
float4 textColor;
float4 panelColor;
uint encodeSrgb;

float3 linearToSrgb(float3 color)
{
    return select(color <= 0.0031308, color * 12.92, 1.055 * pow(max(color, 0), 1.0 / 2.4) - 0.055);
}

float3 srgbToLinear(float3 color)
{
    return select(color <= 0.04045, color / 12.92, pow((max(color, 0) + 0.055) / 1.055, 2.4));
}

// Both execution paths evaluate the same pixel center. Each pixel looks up at
// most one character and one atlas texel; there is no search over glyphs.
float4 overlayColor(float2 pixel)
{
    if (any(pixel < panelOrigin) || any(pixel >= panelEnd))
        return float4(0);
    float coverage = 0;
    float2 position = (pixel - textOrigin) / fontScale;
    if (all(position >= 0))
    {
        uint2 local = uint2(floor(position));
        uint2 cell = local / glyphSize;
        uint2 glyphPixel = local % glyphSize;
        if (all(cell < gridSize))
        {
            uint glyph = characters[cell.y * gridSize.x + cell.x];
            uint2 atlasPixel = uint2(glyph % 16, glyph / 16) * glyphSize + glyphPixel;
            coverage = fontAtlas.Load(int3(atlasPixel, 0));
        }
    }
    float3 foreground = textColor.rgb;
    float3 background = panelColor.rgb;
    if (encodeSrgb != 0)
    {
        foreground = linearToSrgb(foreground);
        background = linearToSrgb(background);
    }
    float alpha = textColor.a * coverage;
    // Compose text over the panel, returning premultiplied RGB and combined alpha.
    return float4(foreground * alpha, alpha) + float4(background * panelColor.a, panelColor.a) * (1 - alpha);
}

[shader("vertex")]
float4 textVertex(uint vertex : SV_VertexID) : SV_Position
{
    float2 uv = float2((vertex << 1) & 2, vertex & 2);
    return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
}

[shader("fragment")]
float4 textFragment(float4 position : SV_Position) : SV_Target
{
    return overlayColor(position.xy);
}

#ifndef TEXT_TARGET_FORMAT
#define TEXT_TARGET_FORMAT "rgba32f"
#endif
#ifndef TEXT_TARGET_RGBA8
#define TEXT_TARGET_RGBA8 0
#endif
[format(TEXT_TARGET_FORMAT)] RWTexture2D<float4> target;
uint2 dispatchOrigin;
uint2 dispatchExtent;
uint targetSrgb;

#if TEXT_TARGET_RGBA8
uint loadPackedRgba8(RWTexture2D<float4> image, uint2 pixel)
{
    __target_switch
    {
    case cuda:
        // Slang 2026.17.1 emits an undefined surf2Dread_convert for unorm reads.
        // Read the four bytes as one word and normalize them explicitly instead.
        __intrinsic_asm "surf2Dread<unsigned int>($0, ($1).x * 4, ($1).y)";
    default:
        return 0;
    }
}
#endif

float4 loadTarget(uint2 pixel)
{
#if TEXT_TARGET_RGBA8
    __target_switch
    {
    case cuda:
        uint packed = loadPackedRgba8(target, pixel);
        return float4(packed & 255, (packed >> 8) & 255, (packed >> 16) & 255, packed >> 24) / 255.0;
    default:
        return target[pixel];
    }
#else
    return target[pixel];
#endif
}

[shader("compute")]
[numthreads(8, 8, 1)]
void textCompute(uint3 tid : SV_DispatchThreadID)
{
    if (any(tid.xy >= dispatchExtent))
        return;
    uint2 pixel = dispatchOrigin + tid.xy;
    float4 overlay = overlayColor(float2(pixel) + 0.5);
    if (overlay.a == 0)
        return;
    // One thread owns this pixel, including both panel and text blending.
    float4 color = loadTarget(pixel);
    if (targetSrgb != 0)
        color.rgb = srgbToLinear(color.rgb);
    color = overlay + color * (1 - overlay.a);
    if (targetSrgb != 0)
        color.rgb = linearToSrgb(color.rgb);
    target[pixel] = color;
}
