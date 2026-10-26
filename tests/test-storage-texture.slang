// test-storage-texture.slang - Writes to storage textures with a declared and an inferred format.

uniform float value;
[format("rgba16f")]
uniform WTexture2D<float4> writeOnly;
uniform RWTexture2D<float> readWrite;

[shader("compute")]
[numthreads(4, 4, 1)]
void computeMain(uint3 tid: SV_DispatchThreadID)
{
    writeOnly.Store(tid.xy, float4(value, value * 2.0, 0.5, 1.0));
    readWrite[tid.xy] = readWrite[tid.xy] + value;
}
