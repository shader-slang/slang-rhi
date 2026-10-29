[shader("compute")]
[numthreads(16, 16, 1)]
void computeMain(
    uint3 tid: SV_DispatchThreadID,
#if defined(SURFACE_FORMAT)
    [format(SURFACE_FORMAT)]
#elif defined(__CUDA__)
    [format("rgba8")]
#endif
    WTexture2D<float4> texture,
    uniform uint2 dim
)
{
    if (any(tid.xy >= dim))
        return;

    float2 uv = (tid.xy + 0.5) / float2(dim);
    texture.Store(tid.xy, float4(fract(uv * 4), 0, 1));
}
