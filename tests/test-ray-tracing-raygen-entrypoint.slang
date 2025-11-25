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
