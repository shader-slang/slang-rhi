struct StorageTextures
{
    [format("rgba16f")] WTexture2D<float4> hdr;
    [format("rgba8")] WTexture3D<float4> volume;
    [format("r32ui")] RWTexture2D<uint> counter;
};

ParameterBlock<StorageTextures> storage;
Texture2D<float4> hdrInput;
Texture3D<float4> volumeInput;
Texture2D<uint> counterInput;
Texture2D<uint4> uintInput;
Texture2D<int4> intInput;
RWStructuredBuffer<float4> results;

// These stay unbound. Programs sharing a module must only require resources
// used by their entry points, including within nested parameter blocks.
Texture2D<float4> unusedTexture;
SamplerState unusedSampler;
StructuredBuffer<float> unusedBuffer;

DepthTexture2D depthInput;
SamplerComparisonState comparisonSampler;

[shader("compute")]
[numthreads(1, 1, 1)]
void compareDepth()
{
    results[0] = float4(
        depthInput.SampleCmpLevelZero(comparisonSampler, float2(0.5), 0.25),
        depthInput.SampleCmpLevelZero(comparisonSampler, float2(0.5), 0.75),
        0,
        0
    );
}

[shader("compute")]
[numthreads(1, 1, 1)]
void writeTextures()
{
    storage.hdr.Store(uint2(0), float4(0.25, 0.5, 2, 1));
    storage.volume.Store(uint3(0, 0, 1), float4(0, 1, 0, 1));
    storage.counter[uint2(0)] += 3;
}

[shader("compute")]
[numthreads(1, 1, 1)]
void readTextures()
{
    results[0] = hdrInput.Load(int3(0));
    results[1] = volumeInput.Load(int4(0, 0, 1, 0));
    results[2] = float4(counterInput.Load(int3(0)), 0, 0, 0);
}

[shader("compute")]
[numthreads(1, 1, 1)]
void readIntegers()
{
    results[0] = float4(uintInput.Load(int3(0)));
    results[1] = float4(intInput.Load(int3(0)));
}

struct PartialBindings
{
    float unusedUniform;
    Texture2D<float4> unusedTexture;
    SamplerState unusedSampler;
    StructuredBuffer<float> unusedBuffer;
    Texture2D<float4> usedTexture;
};

ParameterBlock<PartialBindings> partial;

[shader("compute")]
[numthreads(1, 1, 1)]
void readPartialBlock()
{
    results[0] = partial.usedTexture.Load(int3(0));
}

Texture2D<float4> specializationInputA;
Texture2D<float4> specializationInputB;

interface ITextureReader
{
    float4 read();
};

struct TextureReaderA : ITextureReader
{
    float4 read() { return specializationInputA.Load(int3(0)); }
};

struct TextureReaderB : ITextureReader
{
    float4 read() { return specializationInputB.Load(int3(0)); }
};

[shader("compute")]
[numthreads(1, 1, 1)]
void readSpecialized(uniform ITextureReader reader, uniform uint outputIndex)
{
    results[outputIndex] = reader.read();
}

StructuredBuffer<float4> vertexPositions;
Texture2D<float4> fragmentInput;

[shader("vertex")]
float4 bindingVertex(uint vertexID : SV_VertexID) : SV_Position
{
    return vertexPositions[vertexID];
}

[shader("fragment")]
float4 bindingFragment() : SV_Target
{
    float4 color = fragmentInput.Load(int3(0));
    results[0] = color;
    return color;
}
