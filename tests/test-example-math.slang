StructuredBuffer<float4x4> matrices;
StructuredBuffer<float3x4> affineMatrices;
RWStructuredBuffer<float4> output;

[shader("compute")]
[numthreads(2, 1, 1)]
void computeMain(uint3 tid : SV_DispatchThreadID)
{
    float4 point = float4(2, -1, 3, 1);
    output[2 * tid.x] = mul(matrices[tid.x], point);
    output[2 * tid.x + 1] = float4(mul(affineMatrices[tid.x], point), 1);
}
