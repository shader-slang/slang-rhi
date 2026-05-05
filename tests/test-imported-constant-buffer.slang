import "test-imported-constant-buffer-data";

RWStructuredBuffer<float4> resultBuffer;

[shader("compute")]
[numthreads(1, 1, 1)]
void computeMain(uint3 tid : SV_DispatchThreadID)
{
    resultBuffer[tid.x] = gData.value;
}
