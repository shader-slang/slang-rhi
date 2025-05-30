static const uint N = 1024;

StructuredBuffer<float> globalBuffers[N];
Texture2D<float> globalTextures[N];

struct PB
{
    StructuredBuffer<float> buffers[N];
    Texture2D<float> textures[N];
};

ParameterBlock<PB> pb;

[shader("compute")]
[numthreads(1, 1, 1)]
void computeMain(uint3 tid : SV_DispatchThreadID, StructuredBuffer<float> localBuffers[N], Texture2D<float> localTextures[N], RWStructuredBuffer<float> resultBuffer)
{
    uint index = tid.x;
    if (index >= N)
        return;

    resultBuffer[index * 6 + 0] = globalBuffers[index][0];
    resultBuffer[index * 6 + 1] = globalTextures[index][uint2(0)];
    resultBuffer[index * 6 + 2] = pb.buffers[index][0];
    resultBuffer[index * 6 + 3] = pb.textures[index][uint2(0)];
    resultBuffer[index * 6 + 4] = localBuffers[index][0];
    resultBuffer[index * 6 + 5] = localTextures[index][uint2(0)];
}
