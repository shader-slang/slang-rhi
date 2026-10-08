#include "../examples/raster/environment.slang"

TextureCube<float4> probeSpecular;
TextureCube<float4> probeDiffuse;
Texture2D<float4> probeBrdf;
SamplerState probeSampler;
RWStructuredBuffer<float4> probeResults;
uint probePreset;

[shader("compute")][numthreads(32, 1, 1)] void inspectEnvironment(uint3 tid : SV_DispatchThreadID)
{
    uint i = tid.x;
    if (i >= 128)
        return;
    // Directions independent of the cubemap face-generation convention. This
    // catches swapped/flipped faces and incomplete copies across every mip.
    float y = 1 - 2 * (i + 0.5) / 128;
    float radius = sqrt(1 - y * y);
    float phi = i * 2.39996323;
    float3 direction = float3(radius * cos(phi), y, radius * sin(phi));
    for (uint mip = 0; mip < 9; ++mip)
        probeResults[i * 12 + mip] = probeSpecular.SampleLevel(probeSampler, direction, mip);
    probeResults[i * 12 + 9] = float4(evaluateEnvironment(direction, probePreset), 1);
    probeResults[i * 12 + 10] = probeDiffuse.SampleLevel(probeSampler, direction, 0);
    probeResults[i * 12 + 11] = probeBrdf.SampleLevel(probeSampler, float2((i + 0.5) / 128, frac(i * 0.618034)), 0);
}
