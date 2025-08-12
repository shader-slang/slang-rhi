// This shader explicitly includes the NVAPI header.
// This leads to reflection data containing "g_NvidiaExt" which automatically gets assigned a null-descriptor when binding a root shader object.

#pragma warning(disable: 15205)
#pragma warning(disable: 30081)
#include "nvHLSLExtns.h"

[shader("compute")]
[numthreads(1, 1, 1)]
void computeMain(RWStructuredBuffer<uint> result)
{
    uint2 clock = getRealtimeClock();
    result[0] = 1;
    result[1] = clock.x;
    result[2] = clock.y;
}
