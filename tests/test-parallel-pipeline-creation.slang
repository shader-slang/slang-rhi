// test-parallel-pipeline-creation.slang
// Multiple entry points for testing parallel pipeline resolution.

uniform RWStructuredBuffer<float> buffer;

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

struct VertexOutput
{
    float4 position : SV_Position;
    float4 color : COLOR;
};

[shader("vertex")]
VertexOutput vertexMain(uint vertexId : SV_VertexID)
{
    // Generate a fullscreen triangle from vertex ID.
    float2 pos = float2((vertexId << 1) & 2, vertexId & 2) * 2.0 - 1.0;
    VertexOutput output;
    output.position = float4(pos, 0.0, 1.0);
    output.color = float4(1.0, 0.0, 0.0, 1.0);
    return output;
}

[shader("fragment")]
float4 fragmentMain(float4 color : COLOR) : SV_Target
{
    return color;
}
