uniform RWStructuredBuffer<float> buffer;

[shader("compute")]
[numthreads(1,1,1)]
void computeMain(uint3 sv_dispatchThreadID : SV_DispatchThreadID)
{
    buffer[0] = buffer[0] + 1.0;
}
