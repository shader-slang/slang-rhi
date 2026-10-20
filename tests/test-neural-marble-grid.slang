#include "../examples/neural-marble/neural-marble.slang"

float2 probeUV(uint sample)
{
    // Include domain corners and edges, as well as interior samples.
    return float2(sample % 16, sample / 16) / float2(15, 7);
}

[Differentiable]
float probeLoss(no_diff uint sample)
{
    let encoded = encode(no_diff probeUV(sample), sample);
    float result = 0;
    [MaxIters(MARBLE_INPUTS)]
    for (int i = 0; i < MARBLE_INPUTS; ++i)
        result += encoded.data[i] * float(i + 1) / MARBLE_INPUTS;
    return result;
}

[shader("compute")]
[numthreads(32, 1, 1)]
void backward(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x < MARBLE_BATCH_SIZE)
        bwd_diff(probeLoss)(tid.x, 1.0f);
}

[shader("compute")]
[numthreads(32, 1, 1)]
void forward(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x < MARBLE_BATCH_SIZE)
        validation[tid.x] = probeLoss(tid.x);
}
