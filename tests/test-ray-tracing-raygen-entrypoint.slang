[shader("raygeneration")]
void rayGenResources(uniform RWStructuredBuffer<uint> output)
{
    output[0] = 12345;
}

struct RayGenParameters
{
    uint value;
    RWStructuredBuffer<uint> output;
};

[shader("raygeneration")]
void rayGenConstantBuffer(uniform ConstantBuffer<RayGenParameters> params)
{
    params.output[0] = params.value;
}

[shader("raygeneration")]
void rayGenParameterBlock(uniform ParameterBlock<RayGenParameters> params)
{
    params.output[0] = params.value;
}

[shader("raygeneration")]
void rayGenA(RWStructuredBuffer<uint> output, uniform uint value)
{
    uint3 index = DispatchRaysIndex();
    uint3 dim = DispatchRaysDimensions();
    uint linearIndex = index.y * dim.x + index.x;
    output[linearIndex] = value + linearIndex;
}

[shader("raygeneration")]
void rayGenB(RWStructuredBuffer<uint> output, uniform uint value)
{
    uint3 index = DispatchRaysIndex();
    uint3 dim = DispatchRaysDimensions();
    uint linearIndex = index.y * dim.x + index.x;
    output[linearIndex] = value + linearIndex * 2;
}
