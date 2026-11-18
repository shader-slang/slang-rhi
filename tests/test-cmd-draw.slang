// graphics-smoke.slang

// Per-vertex attributes to be assembled from bound vertex buffers.
struct AssembledVertex
{
    float3	positionA : POSITIONA;
    float3  positionB : POSITIONB;
    float3	color    : COLOR;
};

// Output of the vertex shader, and input to the fragment shader.
struct CoarseVertex
{
    float3 color;
};

// Output of the fragment shader
struct Fragment
{
    float4 color;
};

// Vertex  Shader

struct VertexStageOutput
{
    CoarseVertex    coarseVertex    : CoarseVertex;
    float4          sv_position     : SV_Position;
};

[shader("vertex")]
VertexStageOutput vertexMain(
    AssembledVertex assembledVertex)
{
    VertexStageOutput output;

    float3 position = assembledVertex.positionA + assembledVertex.positionB;
    float3 color    = assembledVertex.color;

    output.coarseVertex.color = color;
    output.sv_position = float4(position, 1.0);

    return output;
}

// Fragment Shader

[shader("fragment")]
float4 fragmentMain(
    CoarseVertex coarseVertex : CoarseVertex) : SV_Target
{
    float3 color = coarseVertex.color;

    return float4(color, 1.0);
}

struct ViewportArrayVertexOutput
{
    float4 position : SV_Position;
    uint viewportIndex : SV_ViewportArrayIndex;
    float3 color : COLOR;
};

[shader("vertex")]
ViewportArrayVertexOutput viewportArrayVertexMain(
    uint vertexID : SV_VertexID,
    uint instanceID : SV_InstanceID)
{
    float2 uv = float2((vertexID << 1) & 2, vertexID & 2);
    ViewportArrayVertexOutput output;
    output.position = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
    output.viewportIndex = instanceID;
    output.color = instanceID == 0 ? float3(1, 0, 0) : float3(0, 0, 1);
    return output;
}

[shader("fragment")]
float4 viewportArrayFragmentMain(ViewportArrayVertexOutput input) : SV_Target
{
    return float4(input.color, 1);
}
