// test-upload-buffer.slang - Reads a buffer in upload memory from a shader.

uniform StructuredBuffer<float> input;
uniform RWStructuredBuffer<float> output;

[shader("compute")]
[numthreads(4, 1, 1)]
void computeMain(uint3 tid: SV_DispatchThreadID)
{
    output[tid.x] = input[tid.x] * 2.0;
}
