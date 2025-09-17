
float3 acesFilm(float3 x)
{
    x *= 0.6;
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

[[shader("compute")]]
[[numthreads(8, 8, 1)]]
void mainCompute(uint3 tid: SV_DispatchThreadID, Texture2D<float4> input, RWTexture2D<float4> output)
{
    uint2 pixel = tid.xy;
    uint2 dims;
    input.GetDimensions(dims.x, dims.y);
    if (any(pixel >= dims))
        return;
    float3 i = input[pixel].xyz;
    float3 o = acesFilm(i);
    output[pixel] = float4(o, 1.0);
}
