// Module A: rayGen takes a single uint parameter.
// output[i] = value + i

[shader("raygeneration")]
void rayGen(RWStructuredBuffer<uint> output, uniform uint value)
{
    uint3 index = DispatchRaysIndex();
    uint3 dim = DispatchRaysDimensions();
    uint linearIndex = index.y * dim.x + index.x;
    output[linearIndex] = value + linearIndex;
}
