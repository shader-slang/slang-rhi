// Simple shader that copies a buffer referred to by src ptr to dst ptr

uniform uint32_t val;
uniform uint32_t* dst;

[shader("compute")]
[numthreads(32,1,1)]
void computeMain(
    uint3 sv_dispatchThreadID : SV_DispatchThreadID)
{
    dst[sv_dispatchThreadID.x] = val;
}
