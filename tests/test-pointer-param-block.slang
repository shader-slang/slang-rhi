// Pointer parameters inside a ParameterBlock — reproduces the layout
// that slangpy generates when dispatching functions with pointer args.

struct CopyParams
{
    uint32_t* src;
    uint32_t* dst;
};

ParameterBlock<CopyParams> copyParams;

[shader("compute")]
[numthreads(32,1,1)]
void copyMain(uint3 tid : SV_DispatchThreadID)
{
    copyParams.dst[tid.x] = copyParams.src[tid.x];
}

// Interleaved value and pointer fields to exercise mixed-category layouts.
struct MixedParams
{
    uint32_t scaleA;
    uint32_t* src;
    uint32_t scaleB;
    uint32_t* dst;
};

ParameterBlock<MixedParams> mixedParams;

[shader("compute")]
[numthreads(32,1,1)]
void mixedMain(uint3 tid : SV_DispatchThreadID)
{
    uint32_t val = mixedParams.src[tid.x];
    mixedParams.dst[tid.x] = val * mixedParams.scaleA + mixedParams.scaleB;
}
