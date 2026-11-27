// The write is reached through a global `ParameterBlock` and guarded by an ordinary entry-point
// `uniform`, so a result of 1 requires both the parameter block's descriptor and the push constant
// the uniform lowers to. See shader-slang/slang#12349.

struct OutputBlock
{
    RWStructuredBuffer<uint> count;
};

ParameterBlock<OutputBlock> output;

[shader("compute")]
[numthreads(1, 1, 1)]
void computeMain(uint3 tid: SV_DispatchThreadID, uniform uint width)
{
    if (tid.x < width)
        output.count[tid.x] = 1u;
}
