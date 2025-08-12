// This shader implicitly includes the NVAPI header (through the use of getRealtimeClock()).
// This leads to reflection data not containing "g_NvidiaExt".
// slang-rhi will automatically detect this and add an extra descriptor range to the root descriptor set and assigns a null-descriptor on binding.

uniform uint globalVar;
StructuredBuffer<uint> globalBuffer;

[shader("compute")]
[numthreads(1, 1, 1)]
void computeMain(StructuredBuffer<uint> buffer, RWStructuredBuffer<uint> result)
{
    result[0] = globalVar;
    result[1] = globalBuffer[0];
    result[2] = buffer[0];
    result[3] = getRealtimeClock().x;
}
