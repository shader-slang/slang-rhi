// trivial-copy-textures.slang

typedef uint4 Element;

// UNORDERED ACCESS VIEW
[shader("compute")]
[numthreads(1,1,1)]
void testRWTexture3D(
    uint3 sv_dispatchThreadID : SV_DispatchThreadID,
    uniform uint width,
    uniform uint height,
    uniform uint depth,
    uniform RWTexture3D<Element> texture,
    uniform RWStructuredBuffer<Element> results)
{
    uint tid = sv_dispatchThreadID.x;

    uint tmp = tid;
    uint x = tmp % width; tmp /= width;
    uint y = tmp % height; tmp /= height;
    uint z = tmp;

    uint3 coord = uint3(x, y, z);

    // Read from texture
    Element elem = texture[coord];

    // Write to texture (if possible)
    texture[coord] = Element(1);

    // Write something to results
    results[tid] = elem;
}

[shader("compute")]
[numthreads(1,1,1)]
void testRWTexture2D(
    uint3 sv_dispatchThreadID : SV_DispatchThreadID,
    uniform uint width,
    uniform uint height,
    uniform uint depth,
    uniform RWTexture2D<Element> texture,
    uniform RWStructuredBuffer<Element> results)
{
    uint tid = sv_dispatchThreadID.x;

    uint tmp = tid;
    uint x = tmp % width; tmp /= width;
    uint y = tmp % height; tmp /= height;
    uint z = tmp;

    uint3 coord = uint3(x, y, z);

    // Read from texture
    Element elem = texture[coord.xy];

    // Write to texture (if possible)
    texture[coord.xy] = Element(1);

    // Write something to results
    results[tid] = elem;
}

[shader("compute")]
[numthreads(1,1,1)]
void testRWTexture1D(
    uint3 sv_dispatchThreadID : SV_DispatchThreadID,
    uniform uint width,
    uniform uint height,
    uniform uint depth,
    uniform RWTexture1D<Element> texture,
    uniform RWStructuredBuffer<Element> results)
{
    uint tid = sv_dispatchThreadID.x;

    uint tmp = tid;
    uint x = tmp % width; tmp /= width;
    uint y = tmp % height; tmp /= height;
    uint z = tmp;

    uint3 coord = uint3(x, y, z);

    // Read from texture
    Element elem = texture[coord.x];

    // Write to texture (if possible)
    texture[coord.x] = Element(1);

    // Write something to results
    results[tid] = elem;
}

// SHADER RESOURCE VIEW
[shader("compute")]
[numthreads(1,1,1)]
void testTexture3D(
    uint3 sv_dispatchThreadID : SV_DispatchThreadID,
    uniform uint width,
    uniform uint height,
    uniform uint depth,
    uniform Texture3D<Element> texture,
    uniform RWStructuredBuffer<Element> results)
{
    uint tid = sv_dispatchThreadID.x;

    uint tmp = tid;
    uint x = tmp % width; tmp /= width;
    uint y = tmp % height; tmp /= height;
    uint z = tmp;

    uint3 coord = uint3(x, y, z);

    // Read from texture
    Element elem = texture[coord];

    // Write something to results
    results[tid] = elem;
}

[shader("compute")]
[numthreads(1,1,1)]
void testTexture2D(
    uint3 sv_dispatchThreadID : SV_DispatchThreadID,
    uniform uint width,
    uniform uint height,
    uniform uint depth,
    uniform Texture2D<Element> texture,
    uniform RWStructuredBuffer<Element> results)
{
    uint tid = sv_dispatchThreadID.x;

    uint tmp = tid;
    uint x = tmp % width; tmp /= width;
    uint y = tmp % height; tmp /= height;
    uint z = tmp;

    uint3 coord = uint3(x, y, z);

    // Read from texture
    Element elem = texture[coord.xy];

    // Write something to results
    results[tid] = elem;
}

[shader("compute")]
[numthreads(1,1,1)]
void testTexture1D(
    uint3 sv_dispatchThreadID : SV_DispatchThreadID,
    uniform uint width,
    uniform uint height,
    uniform uint depth,
    uniform Texture1D<Element> texture,
    uniform RWStructuredBuffer<Element> results)
{
    uint tid = sv_dispatchThreadID.x;

    uint tmp = tid;
    uint x = tmp % width; tmp /= width;
    uint y = tmp % height; tmp /= height;
    uint z = tmp;

    uint3 coord = uint3(x, y, z);

    // Read from texture
    Element elem = texture[coord.x];

    // Write something to results
    results[tid] = elem;
}

// RENDER TARGET AND DEPTH STENCIL VIEWS
// Per-vertex attributes to be assembled from bound vertex buffers.
struct AssembledVertex
{
    float3	position : POSITION;
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

    float3 position = assembledVertex.position;
    float3 color    = assembledVertex.color;

    output.coarseVertex.color = color;
    output.sv_position = float4(position, 1.0);

    return output;
}

// Fragment Shader

[shader("fragment")]
float4 fragmentMain() : SV_Target
{
    return float4(1.0, 2.0, 3.0, 4.0);
}
