
struct TestInput
{
    float2 uv;
    float level;
    float padding;
};

[shader("compute")]
[numthreads(1, 1, 1)]
void sampleTexture(
    uint tid: SV_DispatchThreadID,
    Sampler2D texture,
    StructuredBuffer<TestInput> inputs,
    RWStructuredBuffer<float4> results,
    uniform uint count
)
{
    if (tid >= count)
        return;

    uint index = tid;
    results[index] = texture.SampleLevel(inputs[index].uv, inputs[index].level);
}
