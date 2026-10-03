// Global-scope uniforms form a pipeline's packed global-parameter block. On CUDA
// that block is interned by byte content and uploaded to the SLANG_globalParams
// symbol at most once per distinct packet within a command buffer; `epArg` is an
// entry-point argument, supplied on every launch.
uniform RWStructuredBuffer<float> outputBuffer;
uniform float globalAddend;
uniform uint globalIndex;

[shader("compute")]
[numthreads(1, 1, 1)]
void computeMain(uint3 tid : SV_DispatchThreadID, uniform float epArg)
{
    outputBuffer[globalIndex] = globalAddend + epArg;
}

// A second compute entry point. Distinct SLANG_globalParams symbols come from
// per-pipeline compilation (each pipeline loads its own module), not from this being
// a separate entry point -- entry points in one module share the module's globals.
// The test compiles this into its own pipeline so two pipelines can be interleaved
// to check the per-destination-symbol copy-skip state does not cross-talk.
[shader("compute")]
[numthreads(1, 1, 1)]
void computeMain2(uint3 tid : SV_DispatchThreadID, uniform float epArg)
{
    outputBuffer[globalIndex] = globalAddend + epArg;
}

// The global block here is a single scalar; the output buffer and index are
// entry-point arguments. Submitting a byte-identical global packet from several
// sequential command buffers exercises the intern + copy-skip path on a recycled
// command buffer and checks the result stays correct.
uniform float seqAddend;

[shader("compute")]
[numthreads(1, 1, 1)]
void computeMainSeq(uint3 tid : SV_DispatchThreadID, uniform RWStructuredBuffer<float> seqOut, uniform uint seqIndex)
{
    seqOut[seqIndex] = seqAddend;
}

// An 8 KB global block: distinct packets each occupy most of a 16 KB pool page, so
// a few of them grow the pool's page vector while interned keys still point into
// earlier pages.
uniform float bigData[2048];

[shader("compute")]
[numthreads(1, 1, 1)]
void computeMainBig(uint3 tid : SV_DispatchThreadID)
{
    outputBuffer[globalIndex] = bigData[globalIndex];
}

// A nested parameter block is packed as its own child packet and interned before
// its parent, so byte-equal parents end up holding equal (canonical) child
// addresses.
struct Material
{
    float value;
}

uniform ParameterBlock<Material> material;

[shader("compute")]
[numthreads(1, 1, 1)]
void computeMainNested(uint3 tid : SV_DispatchThreadID)
{
    outputBuffer[globalIndex] = material.value;
}

// The global block here is a single scalar; the output buffer is an entry-point
// argument. A native callback can therefore overwrite the whole SLANG_globalParams
// symbol without touching any device pointer, letting a test check that the
// copy-skip cache is cleared at a callback.
uniform float callbackAddend;

[shader("compute")]
[numthreads(1, 1, 1)]
void computeMainCallback(uint3 tid : SV_DispatchThreadID, uniform RWStructuredBuffer<float> cbOut)
{
    cbOut[0] = callbackAddend;
}
