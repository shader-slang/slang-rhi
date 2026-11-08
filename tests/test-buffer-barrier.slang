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

// One step of a chain: every thread reads a value that another thread wrote in the previous step.
[shader("compute")]
[numthreads(64,1,1)]
void computeStep(
    uint3 sv_dispatchThreadID : SV_DispatchThreadID,
    uniform uint count,
    StructuredBuffer<float> inBuffer,
    RWStructuredBuffer<float> outBuffer)
{
    uint i = sv_dispatchThreadID.x;
    if (i < count)
        outBuffer[i] = inBuffer[(i + 1) % count] + 1.0f;
}
