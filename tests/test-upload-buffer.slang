// test-upload-buffer.slang - Reads and writes buffers in upload memory from shaders.

uniform StructuredBuffer<float> input;
uniform RWStructuredBuffer<float> output;

[shader("compute")]
[numthreads(4, 1, 1)]
void computeMain(uint3 tid: SV_DispatchThreadID)
{
    output[tid.x] = input[tid.x] * 2.0;
}

uniform RWStructuredBuffer<float> target;

[shader("compute")]
[numthreads(4, 1, 1)]
void writeMain(uint3 tid: SV_DispatchThreadID)
{
    target[tid.x] = float(tid.x) + 100.0;
}
