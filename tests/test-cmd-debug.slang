uniform RWStructuredBuffer<float> buffer;

[shader("compute")]
[numthreads(1,1,1)]
void computeMain(uint3 sv_dispatchThreadID : SV_DispatchThreadID)
{
    buffer[0] = buffer[0] + 1.0;
}

[shader("vertex")]
float4 vertexMain() : SV_Position
{
    return float4(0.0, 0.0, 0.0, 1.0);
}

[shader("fragment")]
float4 fragmentMain() : SV_Target
{
    return float4(1.0);
}
