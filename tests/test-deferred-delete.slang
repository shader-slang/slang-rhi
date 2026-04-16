[shader("compute")]
[numthreads(256, 1, 1)]
void writeValue(uint tid : SV_DispatchThreadID, RWStructuredBuffer<uint> buffer, uniform uint value)
{
    buffer[tid] = value;
}

[shader("compute")]
[numthreads(256, 1, 1)]
void accumulate(uint tid : SV_DispatchThreadID, RWStructuredBuffer<uint> accumBuffer, StructuredBuffer<uint> srcBuffer)
{
    accumBuffer[tid] = accumBuffer[tid] + srcBuffer[tid];
}
