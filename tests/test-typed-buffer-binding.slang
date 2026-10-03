// Typed access is intentionally absent. Both bindings must preserve the following uniforms.
Buffer<float> readBinding;
uniform uint2 readGuard;
RWBuffer<uint4> writeBinding;
uniform uint2 writeGuard;
RWStructuredBuffer<uint> result;

[shader("compute")][numthreads(1, 1, 1)] void computeMain()
{
    result[0] = readGuard.x;
    result[1] = readGuard.y;
    result[2] = writeGuard.x;
    result[3] = writeGuard.y;
}
