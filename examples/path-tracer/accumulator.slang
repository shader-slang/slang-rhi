
[[shader("compute")]]
[[numthreads(8, 8, 1)]]
void mainCompute(
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
