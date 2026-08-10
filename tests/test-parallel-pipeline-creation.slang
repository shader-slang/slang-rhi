RWStructuredBuffer<float> buffer;

[shader("compute")]
[numthreads(4, 1, 1)]
void computeAdd(uint3 tid : SV_DispatchThreadID)
{
    buffer[tid.x] += 1.0f;
}

[shader("compute")]
[numthreads(4, 1, 1)]
void computeMul(uint3 tid : SV_DispatchThreadID)
{
    buffer[tid.x] *= 2.0f;
}

[shader("compute")]
[numthreads(4, 1, 1)]
void computeSub(uint3 tid : SV_DispatchThreadID)
{
    buffer[tid.x] -= 0.5f;
}

[shader("compute")]
[numthreads(4, 1, 1)]
void computeNeg(uint3 tid : SV_DispatchThreadID)
{
    buffer[tid.x] = -buffer[tid.x];
}
