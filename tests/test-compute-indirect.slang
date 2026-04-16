// The shader increments a counter in the output buffer for each thread that runs.
// By using different indirect dispatch arguments, we can verify the correct number of threads are dispatched.

#include "../include/slang-rhi-device.h"

[shader("compute")]
[numthreads(4, 4, 1)]
void computeMain(uint3 tid: SV_DispatchThreadID, RWByteAddressBuffer outputBuffer, uniform uint dispatchIndex)
{
    // Each thread increments the counter at its dispatch index
    // This allows us to verify that the correct number of threads ran for each dispatch
    outputBuffer.InterlockedAdd(dispatchIndex * sizeof(uint), 1);
}

// Entry point to write dispatch arguments from the GPU
[shader("compute")]
[numthreads(1, 1, 1)]
void writeDispatchArgs(
    RWStructuredBuffer<rhi::IndirectDispatchArguments> dispatchArgsBuffer,
    uniform uint dispatchIndex,
    uniform uint3 threadGroupCounts
)
{
    dispatchArgsBuffer[dispatchIndex].threadGroupCountX = threadGroupCounts.x;
    dispatchArgsBuffer[dispatchIndex].threadGroupCountY = threadGroupCounts.y;
    dispatchArgsBuffer[dispatchIndex].threadGroupCountZ = threadGroupCounts.z;
}
