// Simple shader that copies a buffer referred to by src ptr to dst ptr

uniform StructuredBuffer<uint32_t> src;
uniform RWStructuredBuffer<uint32_t> dst;

[shader("compute")]
[numthreads(32,1,1)]
void computeMain(
    uint3 sv_dispatchThreadID : SV_DispatchThreadID)
{
    var input = src[sv_dispatchThreadID.x];
    dst[sv_dispatchThreadID.x] = input;
}
