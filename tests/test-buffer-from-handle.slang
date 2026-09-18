// Typed RWBuffer (not a structured buffer) so the Vulkan backend must build a texel VkBufferView.
// Regression coverage for shader-slang/slang-rhi#860.

RWBuffer<float> buffer;

[shader("compute")]
[numthreads(4, 1, 1)]
void computeMain(uint3 tid: SV_DispatchThreadID)
{
    buffer[tid.x] = buffer[tid.x] + 1.0f;
}
