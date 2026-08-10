// Reproduces the slangpy pattern where pointers live inside a struct
// (like ValueType<float*>) and are bound as ordinary data through a
// ConstantBuffer sub-object.

struct PtrPair
{
    float* src;
    float* dst;
};

uniform PtrPair ptrs;

[shader("compute")]
[numthreads(32, 1, 1)]
void computeMain(uint3 tid : SV_DispatchThreadID)
{
    ptrs.dst[tid.x] = ptrs.src[tid.x];
}
