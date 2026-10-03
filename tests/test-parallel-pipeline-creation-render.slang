struct VertexOutput
{
    float4 position : SV_Position;
};

[shader("vertex")]
VertexOutput vertexMain(uint vertexID : SV_VertexID)
{
    static const float2 positions[] = {
        float2(-1.0f, -1.0f),
        float2(3.0f, -1.0f),
        float2(-1.0f, 3.0f),
    };
    VertexOutput output;
    output.position = float4(positions[vertexID], 0.0f, 1.0f);
    return output;
}

[shader("fragment")]
float4 fragmentMain() : SV_Target
{
    return float4(1.0f, 0.0f, 0.0f, 1.0f);
}
