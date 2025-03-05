Buffer<int> buffer1;
Buffer<int> buffer2;
RWBuffer<int> rwBuffer1;
RWBuffer<int> rwBuffer2;
StructuredBuffer<int> structuredBuffer1;
StructuredBuffer<int> structuredBuffer2;
RWStructuredBuffer<int> rwStructuredBuffer1;
RWStructuredBuffer<int> rwStructuredBuffer2;
Texture2D<float> texture1;
Texture2D<float> texture2;
RWTexture2D<float> rwTexture1;
RWTexture2D<float> rwTexture2;
Texture2DArray<float> textureArray1;
Texture2DArray<float> textureArray2;
RWTexture2DArray<float> rwTextureArray1;
RWTexture2DArray<float> rwTextureArray2;
SamplerState samplerState1;
SamplerState samplerState2;

[shader("compute")]
[numthreads(1, 1, 1)]
void compute_main() {}
