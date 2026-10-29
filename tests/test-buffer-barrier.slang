// buffer-barrier-test.slang

[shader("compute")]
[numthreads(4,1,1)]
void computeA(
    uint3 sv_dispatchThreadID : SV_DispatchThreadID,
    StructuredBuffer<float> inBuffer,
    RWStructuredBuffer<float> outBuffer)
{
    outBuffer[sv_dispatchThreadID.x] = inBuffer[sv_dispatchThreadID.x];
}

[shader("compute")]
[numthreads(4,1,1)]
void computeB(
    uint3 sv_dispatchThreadID : SV_DispatchThreadID,
    StructuredBuffer<float> inBuffer,
    RWStructuredBuffer<float> outBuffer)
{
    outBuffer[sv_dispatchThreadID.x] = inBuffer[sv_dispatchThreadID.x] + 10.0f;
}

[shader("compute")]
[numthreads(4,1,1)]
void computeABindless(
    uint3 sv_dispatchThreadID : SV_DispatchThreadID,
    StructuredBuffer<float> inBuffer,
    uniform RWStructuredBuffer<float>.Handle outBuffer)
{
    outBuffer[sv_dispatchThreadID.x] = inBuffer[sv_dispatchThreadID.x];
}


[shader("compute")]
[numthreads(4,1,1)]
void computeBBindless(
    uint3 sv_dispatchThreadID : SV_DispatchThreadID,
    uniform StructuredBuffer<float>.Handle inBuffer,
    RWStructuredBuffer<float> outBuffer)
{
    outBuffer[sv_dispatchThreadID.x] = inBuffer[sv_dispatchThreadID.x] + 10.0f;
}

[shader("compute")]
[numthreads(4,1,1)]
void computeAPointer(
    uint3 sv_dispatchThreadID : SV_DispatchThreadID,
    StructuredBuffer<float> inBuffer,
    uniform float* outBuffer)
{
    outBuffer[sv_dispatchThreadID.x] = inBuffer[sv_dispatchThreadID.x];
}


[shader("compute")]
[numthreads(4,1,1)]
void computeBPointer(
    uint3 sv_dispatchThreadID : SV_DispatchThreadID,
    uniform float* inBuffer,
    RWStructuredBuffer<float> outBuffer)
{
    outBuffer[sv_dispatchThreadID.x] = inBuffer[sv_dispatchThreadID.x] + 10.0f;
}
