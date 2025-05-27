StructuredBuffer<float> globalBuffer;
Texture2D<float> globalTexture;
SamplerState globalSampler;

[shader("compute")]
[numthreads(1, 1, 1)]
void computeMain( StructuredBuffer<float> buffer, Texture2D<float> texture, SamplerState sampler, RWStructuredBuffer<float> resultBuffer)
{
    resultBuffer[0] = globalBuffer[0];
    resultBuffer[1] = globalTexture.SampleLevel(globalSampler, float2(0.25), 0.0);
    resultBuffer[2] = buffer[0];
    resultBuffer[3] = texture.SampleLevel(sampler, float2(0.5), 0.0);
}
