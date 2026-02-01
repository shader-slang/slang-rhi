uniform Sampler2D<float>.Handle combinedSampler2DPoint;
uniform Sampler2D<float>.Handle combinedSampler2DLinear;
uniform Sampler3D<float>.Handle combinedSampler3D;
uniform SamplerCube<float>.Handle combinedSamplerCube;

uniform Sampler2DArray<float>.Handle combinedSampler2DArray;
uniform SamplerCubeArray<float>.Handle combinedSamplerCubeArray;

uniform RWStructuredBuffer<float> result;

[shader("compute")]
[numthreads(1, 1, 1)]
void computeMain()
{
    uint index = 0;

    // Sampler2D with point sampling
    {
        result[index++] = combinedSampler2DPoint.SampleLevel(float2(0.25, 0.25), 0);
        result[index++] = combinedSampler2DPoint.SampleLevel(float2(0.75, 0.75), 0);
    }

    // Sampler2D with linear sampling (samples at center to get average of all 4 texels)
    {
        result[index++] = combinedSampler2DLinear.SampleLevel(float2(0.5, 0.5), 0);
    }

    // Sampler3D
    {
        result[index++] = combinedSampler3D.SampleLevel(float3(0.25, 0.25, 0.25), 0);
        result[index++] = combinedSampler3D.SampleLevel(float3(0.75, 0.75, 0.75), 0);
    }

    // SamplerCube
    {
        result[index++] = combinedSamplerCube.SampleLevel(float3(1, 0, 0), 0);
        result[index++] = combinedSamplerCube.SampleLevel(float3(-1, 0, 0), 0);
        result[index++] = combinedSamplerCube.SampleLevel(float3(0, 1, 0), 0);
        result[index++] = combinedSamplerCube.SampleLevel(float3(0, -1, 0), 0);
        result[index++] = combinedSamplerCube.SampleLevel(float3(0, 0, 1), 0);
        result[index++] = combinedSamplerCube.SampleLevel(float3(0, 0, -1), 0);
    }

    // Sampler2DArray
    {
        result[index++] = combinedSampler2DArray.SampleLevel(float3(0.25, 0.25, 0), 0);
        result[index++] = combinedSampler2DArray.SampleLevel(float3(0.75, 0.75, 0), 0);

        result[index++] = combinedSampler2DArray.SampleLevel(float3(0.25, 0.25, 1), 0);
        result[index++] = combinedSampler2DArray.SampleLevel(float3(0.75, 0.75, 1), 0);
    }

    // SamplerCubeArray
    {
        result[index++] = combinedSamplerCubeArray.SampleLevel(float4(1, 0, 0, 0), 0);
        result[index++] = combinedSamplerCubeArray.SampleLevel(float4(-1, 0, 0, 0), 0);
        result[index++] = combinedSamplerCubeArray.SampleLevel(float4(0, 1, 0, 0), 0);
        result[index++] = combinedSamplerCubeArray.SampleLevel(float4(0, -1, 0, 0), 0);
        result[index++] = combinedSamplerCubeArray.SampleLevel(float4(0, 0, 1, 0), 0);
        result[index++] = combinedSamplerCubeArray.SampleLevel(float4(0, 0, -1, 0), 0);

        result[index++] = combinedSamplerCubeArray.SampleLevel(float4(1, 0, 0, 1), 0);
        result[index++] = combinedSamplerCubeArray.SampleLevel(float4(-1, 0, 0, 1), 0);
        result[index++] = combinedSamplerCubeArray.SampleLevel(float4(0, 1, 0, 1), 0);
        result[index++] = combinedSamplerCubeArray.SampleLevel(float4(0, -1, 0, 1), 0);
        result[index++] = combinedSamplerCubeArray.SampleLevel(float4(0, 0, 1, 1), 0);
        result[index++] = combinedSamplerCubeArray.SampleLevel(float4(0, 0, -1, 1), 0);
    }
}
