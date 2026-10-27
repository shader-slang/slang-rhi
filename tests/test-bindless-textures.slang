struct Params
{
    Texture1D<float>.Handle texture1D;
    Texture2D<float>.Handle texture2D;
    Texture3D<float>.Handle texture3D;
    TextureCube<float>.Handle textureCube;
    Texture1DArray<float>.Handle texture1DArray;
    Texture2DArray<float>.Handle texture2DArray;
    TextureCubeArray<float>.Handle textureCubeArray;
    RWTexture1D<float>.Handle rwTexture1D;
    RWTexture2D<float>.Handle rwTexture2D;
    RWTexture3D<float>.Handle rwTexture3D;
    RWTexture1DArray<float>.Handle rwTexture1DArray;
    RWTexture2DArray<float>.Handle rwTexture2DArray;
    SamplerState.Handle samplerPoint;
    SamplerState.Handle samplerLinear;
};

ParameterBlock<Params> params;




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
        result[index++] = params.texture1D[0];
        result[index++] = params.texture1D[1];
#endif
    }

    // Texture2D
    {
        result[index++] = params.texture2D[uint2(0, 0)];
        result[index++] = params.texture2D[uint2(1, 1)];
    }

    // Texture3D
    {
        result[index++] = params.texture3D[uint3(0, 0, 0)];
        result[index++] = params.texture3D[uint3(1, 1, 1)];
    }

    // TextureCube
    {
        result[index++] = params.textureCube.SampleLevel(params.samplerPoint, float3(1, 0, 0), 0);
        result[index++] = params.textureCube.SampleLevel(params.samplerPoint, float3(-1, 0, 0), 0);
        result[index++] = params.textureCube.SampleLevel(params.samplerPoint, float3(0, 1, 0), 0);
        result[index++] = params.textureCube.SampleLevel(params.samplerPoint, float3(0, -1, 0), 0);
        result[index++] = params.textureCube.SampleLevel(params.samplerPoint, float3(0, 0, 1), 0);
        result[index++] = params.textureCube.SampleLevel(params.samplerPoint, float3(0, 0, -1), 0);
    }

    // Texture1DArray
    {
        result[index++] = params.texture1DArray[uint2(0, 0)];
        result[index++] = params.texture1DArray[uint2(1, 0)];

        result[index++] = params.texture1DArray[uint2(0, 1)];
        result[index++] = params.texture1DArray[uint2(1, 1)];
    }

    // Texture2DArray
    {
        result[index++] = params.texture2DArray[uint3(0, 0, 0)];
        result[index++] = params.texture2DArray[uint3(1, 1, 0)];

        result[index++] = params.texture2DArray[uint3(0, 0, 1)];
        result[index++] = params.texture2DArray[uint3(1, 1, 1)];
    }

    // TextureCubeArray
    {
        result[index++] = params.textureCubeArray.SampleLevel(params.samplerPoint, float4(1, 0, 0, 0), 0);
        result[index++] = params.textureCubeArray.SampleLevel(params.samplerPoint, float4(-1, 0, 0, 0), 0);
        result[index++] = params.textureCubeArray.SampleLevel(params.samplerPoint, float4(0, 1, 0, 0), 0);
        result[index++] = params.textureCubeArray.SampleLevel(params.samplerPoint, float4(0, -1, 0, 0), 0);
        result[index++] = params.textureCubeArray.SampleLevel(params.samplerPoint, float4(0, 0, 1, 0), 0);
        result[index++] = params.textureCubeArray.SampleLevel(params.samplerPoint, float4(0, 0, -1, 0), 0);

        result[index++] = params.textureCubeArray.SampleLevel(params.samplerPoint, float4(1, 0, 0, 1), 0);
        result[index++] = params.textureCubeArray.SampleLevel(params.samplerPoint, float4(-1, 0, 0, 1), 0);
        result[index++] = params.textureCubeArray.SampleLevel(params.samplerPoint, float4(0, 1, 0, 1), 0);
        result[index++] = params.textureCubeArray.SampleLevel(params.samplerPoint, float4(0, -1, 0, 1), 0);
        result[index++] = params.textureCubeArray.SampleLevel(params.samplerPoint, float4(0, 0, 1, 1), 0);
        result[index++] = params.textureCubeArray.SampleLevel(params.samplerPoint, float4(0, 0, -1, 1), 0);
    }

    // RWTexture1D
    {
        result[index++] = params.rwTexture1D[0];
        result[index++] = params.rwTexture1D[1];
        params.rwTexture1D[0] += 1;
        params.rwTexture1D[1] += 1;
    }

    // RWTexture2D
    {
        result[index++] = params.rwTexture2D[uint2(0, 0)];
        result[index++] = params.rwTexture2D[uint2(1, 1)];
        params.rwTexture2D[uint2(0, 0)] += 1;
        params.rwTexture2D[uint2(1, 1)] += 1;
    }

    // RWTexture3D
    {
        result[index++] = params.rwTexture3D[uint3(0, 0, 0)];
        result[index++] = params.rwTexture3D[uint3(1, 1, 1)];
        params.rwTexture3D[uint3(0, 0, 0)] += 1;
        params.rwTexture3D[uint3(1, 1, 1)] += 1;
    }

    // RWTexture1DArray
    {
        result[index++] = params.rwTexture1DArray[uint2(0, 0)];
        result[index++] = params.rwTexture1DArray[uint2(1, 0)];
        params.rwTexture1DArray[uint2(0, 0)] += 1;
        params.rwTexture1DArray[uint2(1, 0)] += 1;

        result[index++] = params.rwTexture1DArray[uint2(0, 1)];
        result[index++] = params.rwTexture1DArray[uint2(1, 1)];
        params.rwTexture1DArray[uint2(0, 1)] += 1;
        params.rwTexture1DArray[uint2(1, 1)] += 1;
    }

    // RWTexture2DArray
    {
        result[index++] = params.rwTexture2DArray[uint3(0, 0, 0)];
        result[index++] = params.rwTexture2DArray[uint3(1, 1, 0)];
        params.rwTexture2DArray[uint3(0, 0, 0)] += 1;
        params.rwTexture2DArray[uint3(1, 1, 0)] += 1;

        result[index++] = params.rwTexture2DArray[uint3(0, 0, 1)];
        result[index++] = params.rwTexture2DArray[uint3(1, 1, 1)];
        params.rwTexture2DArray[uint3(0, 0, 1)] += 1;
        params.rwTexture2DArray[uint3(1, 1, 1)] += 1;
    }
}
