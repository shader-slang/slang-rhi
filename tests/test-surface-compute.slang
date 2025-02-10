[shader("compute")]
[numthreads(16, 16, 1)]
void computeMain(uint3 tid : SV_DispatchThreadID, RWTexture2D<float4> texture, uniform uint2 dim)
{
    if (any(tid.xy >= dim))
        return;

    float2 uv = (tid.xy + 0.5) / float2(dim);
    texture[tid.xy] = float4(uv, 0, 1);
}
