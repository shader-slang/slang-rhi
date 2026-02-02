// The shader increments a counter in the output buffer for each thread that runs.
// By using different indirect dispatch arguments, we can verify the correct number of threads are dispatched.

[shader("compute")]
[numthreads(4, 4, 1)]
void computeMain(uint3 tid: SV_DispatchThreadID, RWByteAddressBuffer outputBuffer, uniform uint dispatchIndex)
{
    // Each thread increments the counter at its dispatch index
    // This allows us to verify that the correct number of threads ran for each dispatch
    uint linearIndex = tid.x + tid.y * 64;
    outputBuffer.InterlockedAdd(dispatchIndex * sizeof(uint), 1);
}
