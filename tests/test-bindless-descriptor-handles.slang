uniform Buffer<float>.Handle buffer;
uniform StructuredBuffer<float>.Handle structuredBuffer;
uniform ByteAddressBuffer.Handle byteAddressBuffer;

uniform RWBuffer<float>.Handle rwBuffer;
uniform RWStructuredBuffer<float>.Handle rwStructuredBuffer;
uniform RWByteAddressBuffer.Handle rwByteAddressBuffer;

uniform Texture1D<float>.Handle texture1D;
uniform Texture2D<float>.Handle texture2D;
uniform Texture3D<float>.Handle texture3D;
uniform TextureCube<float>.Handle textureCube;

uniform Texture1DArray<float>.Handle texture1DArray;
uniform Texture2DArray<float>.Handle texture2DArray;
uniform TextureCubeArray<float>.Handle textureCubeArray;

uniform RWTexture1D<float>.Handle rwTexture1D;
uniform RWTexture2D<float>.Handle rwTexture2D;
uniform RWTexture3D<float>.Handle rwTexture3D;

uniform RWTexture1DArray<float>.Handle rwTexture1DArray;
uniform RWTexture2DArray<float>.Handle rwTexture2DArray;

uniform SamplerState.Handle samplerPoint;
uniform SamplerState.Handle samplerLinear;

uniform RWStructuredBuffer<float>.Handle result;

[shader("compute")]
[numthreads(1, 1, 1)]
void computeMain()
{
    uint index = 0;

    // Buffer
    {
        result[index++] = buffer[0];
        result[index++] = buffer[1];
    }

    // StructuredBuffer
    {
        result[index++] = structuredBuffer[0];
        result[index++] = structuredBuffer[1];
    }

    // ByteAddressBuffer
    {
        result[index++] = byteAddressBuffer.Load<float>(0);
        result[index++] = byteAddressBuffer.Load<float>(4);
    }

    // RWBuffer
    {
        result[index++] = rwBuffer[0];
        result[index++] = rwBuffer[1];
        rwBuffer[0] += 1;
        rwBuffer[1] += 1;
    }

    // RWStructuredBuffer
    {
        result[index++] = rwStructuredBuffer[0];
        result[index++] = rwStructuredBuffer[1];
        rwStructuredBuffer[0] += 1;
        rwStructuredBuffer[1] += 1;
    }

    // RWByteAddressBuffer
    {
        result[index++] = rwByteAddressBuffer.Load<float>(0);
        result[index++] = rwByteAddressBuffer.Load<float>(4);
        rwByteAddressBuffer.Store<float>(0, rwByteAddressBuffer.Load<float>(0) + 1);
        rwByteAddressBuffer.Store<float>(4, rwByteAddressBuffer.Load<float>(4) + 1);
    }

    // Texture1D
    {
        result[index++] = texture1D[0];
        result[index++] = texture1D[1];
    }

    // Texture2D
    {
        result[index++] = texture2D[uint2(0, 0)];
        result[index++] = texture2D[uint2(1, 1)];
    }

    // Texture3D
    {
        result[index++] = texture3D[uint3(0, 0, 0)];
        result[index++] = texture3D[uint3(1, 1, 1)];
    }

    // TextureCube
    {
        // Slang currently puts the sampler descriptors into binding 0 instead of binding 1,
        // so we cannot use samplers at this time in Vulkan.
#if defined(__VULKAN__)
        result[index++] = 1;
        result[index++] = 2;
        result[index++] = 3;
        result[index++] = 4;
        result[index++] = 5;
        result[index++] = 6;
#else
        result[index++] = textureCube.SampleLevel(samplerPoint, float3(1, 0, 0), 0);
        result[index++] = textureCube.SampleLevel(samplerPoint, float3(-1, 0, 0), 0);
        result[index++] = textureCube.SampleLevel(samplerPoint, float3(0, 1, 0), 0);
        result[index++] = textureCube.SampleLevel(samplerPoint, float3(0, -1, 0), 0);
        result[index++] = textureCube.SampleLevel(samplerPoint, float3(0, 0, 1), 0);
        result[index++] = textureCube.SampleLevel(samplerPoint, float3(0, 0, -1), 0);
#endif
    }

    // Texture1DArray
    {
        result[index++] = texture1DArray[uint2(0, 0)];
        result[index++] = texture1DArray[uint2(1, 0)];

        result[index++] = texture1DArray[uint2(0, 1)];
        result[index++] = texture1DArray[uint2(1, 1)];
    }

    // Texture2DArray
    {
        result[index++] = texture2DArray[uint3(0, 0, 0)];
        result[index++] = texture2DArray[uint3(1, 1, 0)];

        result[index++] = texture2DArray[uint3(0, 0, 1)];
        result[index++] = texture2DArray[uint3(1, 1, 1)];
    }

    // TextureCubeArray
    {
        // Slang currently puts the sampler descriptors into binding 0 instead of binding 1,
        // so we cannot use samplers at this time in Vulkan.
#if defined(__VULKAN__)
        result[index++] = 1;
        result[index++] = 2;
        result[index++] = 3;
        result[index++] = 4;
        result[index++] = 5;
        result[index++] = 6;

        result[index++] = 7;
        result[index++] = 8;
        result[index++] = 9;
        result[index++] = 10;
        result[index++] = 11;
        result[index++] = 12;
#else
        result[index++] = textureCubeArray.SampleLevel(samplerPoint, float4(1, 0, 0, 0), 0);
        result[index++] = textureCubeArray.SampleLevel(samplerPoint, float4(-1, 0, 0, 0), 0);
        result[index++] = textureCubeArray.SampleLevel(samplerPoint, float4(0, 1, 0, 0), 0);
        result[index++] = textureCubeArray.SampleLevel(samplerPoint, float4(0, -1, 0, 0), 0);
        result[index++] = textureCubeArray.SampleLevel(samplerPoint, float4(0, 0, 1, 0), 0);
        result[index++] = textureCubeArray.SampleLevel(samplerPoint, float4(0, 0, -1, 0), 0);

        result[index++] = textureCubeArray.SampleLevel(samplerPoint, float4(1, 0, 0, 1), 0);
        result[index++] = textureCubeArray.SampleLevel(samplerPoint, float4(-1, 0, 0, 1), 0);
        result[index++] = textureCubeArray.SampleLevel(samplerPoint, float4(0, 1, 0, 1), 0);
        result[index++] = textureCubeArray.SampleLevel(samplerPoint, float4(0, -1, 0, 1), 0);
        result[index++] = textureCubeArray.SampleLevel(samplerPoint, float4(0, 0, 1, 1), 0);
        result[index++] = textureCubeArray.SampleLevel(samplerPoint, float4(0, 0, -1, 1), 0);
#endif
    }

    // RWTexture1D
    {
        result[index++] = rwTexture1D[0];
        result[index++] = rwTexture1D[1];
        rwTexture1D[0] += 1;
        rwTexture1D[1] += 1;
    }

    // RWTexture2D
    {
        result[index++] = rwTexture2D[uint2(0, 0)];
        result[index++] = rwTexture2D[uint2(1, 1)];
        rwTexture2D[uint2(0, 0)] += 1;
        rwTexture2D[uint2(1, 1)] += 1;
    }

    // RWTexture3D
    {
        result[index++] = rwTexture3D[uint3(0, 0, 0)];
        result[index++] = rwTexture3D[uint3(1, 1, 1)];
        rwTexture3D[uint3(0, 0, 0)] += 1;
        rwTexture3D[uint3(1, 1, 1)] += 1;
    }

    // RWTexture1DArray
    {
        result[index++] = rwTexture1DArray[uint2(0, 0)];
        result[index++] = rwTexture1DArray[uint2(1, 0)];
        rwTexture1DArray[uint2(0, 0)] += 1;
        rwTexture1DArray[uint2(1, 0)] += 1;

        result[index++] = rwTexture1DArray[uint2(0, 1)];
        result[index++] = rwTexture1DArray[uint2(1, 1)];
        rwTexture1DArray[uint2(0, 1)] += 1;
        rwTexture1DArray[uint2(1, 1)] += 1;
    }

    // RWTexture2DArray
    {
        result[index++] = rwTexture2DArray[uint3(0, 0, 0)];
        result[index++] = rwTexture2DArray[uint3(1, 1, 0)];
        rwTexture2DArray[uint3(0, 0, 0)] += 1;
        rwTexture2DArray[uint3(1, 1, 0)] += 1;

        result[index++] = rwTexture2DArray[uint3(0, 0, 1)];
        result[index++] = rwTexture2DArray[uint3(1, 1, 1)];
        rwTexture2DArray[uint3(0, 0, 1)] += 1;
        rwTexture2DArray[uint3(1, 1, 1)] += 1;
    }
}
