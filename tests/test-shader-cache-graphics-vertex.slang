// shader-cache-graphics-vertex.slang

import test_shader_cache_graphics_common;

// Per-vertex attributes to be assembled from bound vertex buffers.
struct AssembledVertex
{
    float3	position : POSITION;
};

// Output of the vertex shader, and input to the fragment shader.
struct CoarseVertex
{
    float3 color;
};

// Vertex  Shader

struct VertexStageOutput
{
    CoarseVertex    coarseVertex    : CoarseVertex;
    float4          sv_position     : SV_Position;
};

[shader("vertex")]
VertexStageOutput main(
    AssembledVertex assembledVertex)
{
    VertexStageOutput output;

    float3 position = assembledVertex.position;
    float3 color    = getShaderCacheTestColor();

    output.coarseVertex.color = color;
    output.sv_position = float4(position, 1.0);

    return output;
}
