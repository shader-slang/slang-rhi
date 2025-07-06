// compute-trivial.slang - Simple shader that takes a buffer and increments all elements by 1.

uniform uint32_t* src;
uniform uint32_t* dst;

[shader("compute")]
[numthreads(32,1,1)]
void computeMain(
    uint3 sv_dispatchThreadID : SV_DispatchThreadID)
{
    var input = src[sv_dispatchThreadID.x];
    dst[sv_dispatchThreadID.x] = input;
}
