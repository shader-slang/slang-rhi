Buffer<float> buffer1;
Buffer<float> buffer2;
Buffer<float> buffer3;
RWBuffer<float> rwBuffer1;
RWBuffer<float> rwBuffer2;
RWBuffer<float> rwBuffer3;
StructuredBuffer<float> structuredBuffer1;
StructuredBuffer<float> structuredBuffer2;
StructuredBuffer<float> structuredBuffer3;
RWStructuredBuffer<float> rwStructuredBuffer1;
RWStructuredBuffer<float> rwStructuredBuffer2;
RWStructuredBuffer<float> rwStructuredBuffer3;
Texture2D<float> texture1;
Texture2D<float> texture2;
Texture2D<float> texture3;
RWTexture2D<float> rwTexture1;
RWTexture2D<float> rwTexture2;
RWTexture2D<float> rwTexture3;
Texture2DArray<float> textureArray1;
Texture2DArray<float> textureArray2;
Texture2DArray<float> textureArray3;
RWTexture2DArray<float> rwTextureArray1;
RWTexture2DArray<float> rwTextureArray2;
RWTexture2DArray<float> rwTextureArray3;
SamplerState samplerState1;
SamplerState samplerState2;
SamplerState samplerState3;
#if defined(__D3D12__) || defined(__VULKAN__)
RaytracingAccelerationStructure as1;
RaytracingAccelerationStructure as2;
RaytracingAccelerationStructure as3;
#endif

RWStructuredBuffer<float> result;

[shader("compute")]
[numthreads(1, 1, 1)]
void computeMain()
{
    // TODO: CUDA/Metal don't fully support typed buffers yet
#if !(defined(__CUDA__) || defined(__METAL__))
    result[0] = buffer2[0];
    result[1] = rwBuffer2[0];
#else
    result[0] = 1.0;
    result[1] = 2.0;
#endif
    result[2] = structuredBuffer2[0];
    result[3] = rwStructuredBuffer2[0];
    result[4] = texture2.Load(int3(0));
    result[5] = rwTexture2.Load(int2(0));
    result[6] = textureArray2.Load(int4(0));
    result[7] = rwTextureArray2.Load(int3(0));
    result[8] = texture2.SampleLevel(samplerState2, float2(0), 0.f);
}
