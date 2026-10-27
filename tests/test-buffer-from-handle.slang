// Typed buffers exercise Vulkan buffer view creation and destruction.

RWBuffer<float> buffer;

[shader("compute")]
[numthreads(4, 1, 1)]
void computeMain(uint3 tid: SV_DispatchThreadID)
{
    buffer[tid.x] = buffer[tid.x] + 1.0f;
}
