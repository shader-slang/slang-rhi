// This shader explicitly includes the NVAPI header.
// This leads to reflection data containing "g_NvidiaExt" which automatically gets assigned a null-descriptor when binding a root shader object.

#pragma warning(disable: 15205)
#pragma warning(disable: 30081)
#include "nvHLSLExtns.h"

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
