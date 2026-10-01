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

struct NestedPointers { PtrPair pairs[2]; uint selector; };
uniform NestedPointers nested;
[shader("compute")]
[numthreads(32, 1, 1)]
void nestedMain(uint3 tid : SV_DispatchThreadID)
{
    uint index = (tid.x + nested.selector) & 1;
    nested.pairs[index].dst[tid.x] = nested.pairs[index].src[tid.x];
}
