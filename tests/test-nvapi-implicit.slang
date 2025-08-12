// This shader implicitly includes the NVAPI header (through the use of getRealtimeClock()).
// This leads to reflection data not containing "g_NvidiaExt".
// slang-rhi will automatically detect this and add an extra descriptor range to the root descriptor set and assigns a null-descriptor on binding.

[shader("compute")]
[numthreads(1, 1, 1)]
void computeMain(RWStructuredBuffer<uint> result)
{
    uint2 clock = getRealtimeClock();
    result[0] = 1;
    result[1] = clock.x;
    result[2] = clock.y;
}
