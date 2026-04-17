// Module B: rayGen takes two uint parameters.
// output[i] = value + i * multiplier

[shader("raygeneration")]
void rayGen(RWStructuredBuffer<uint> output, uniform uint value, uniform uint multiplier)
{
    uint3 index = DispatchRaysIndex();
    uint3 dim = DispatchRaysDimensions();
    uint linearIndex = index.y * dim.x + index.x;
    output[linearIndex] = value + linearIndex * multiplier;
}
