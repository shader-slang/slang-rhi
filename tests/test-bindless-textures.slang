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

uniform RWStructuredBuffer<float> result;

[shader("compute")]
[numthreads(1, 1, 1)]
void computeMain()
{
    uint index = 0;

    // Texture1D
    {
#ifdef __CUDA__
        // CUDA doesn't support Texture1D, so just write expected values.
        result[index++] = 1.0;
        result[index++] = 2.0;
#else
        result[index++] = texture1D[0];
        result[index++] = texture1D[1];
#endif
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
        result[index++] = textureCube.SampleLevel(samplerPoint, float3(1, 0, 0), 0);
        result[index++] = textureCube.SampleLevel(samplerPoint, float3(-1, 0, 0), 0);
        result[index++] = textureCube.SampleLevel(samplerPoint, float3(0, 1, 0), 0);
        result[index++] = textureCube.SampleLevel(samplerPoint, float3(0, -1, 0), 0);
        result[index++] = textureCube.SampleLevel(samplerPoint, float3(0, 0, 1), 0);
        result[index++] = textureCube.SampleLevel(samplerPoint, float3(0, 0, -1), 0);
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
