// Mirrors the shape that surfaced shader-slang/slang#10893: a struct behind a
// ConstantBuffer, whose element type layout a caller can pull out of the program
// layout and hand to createShaderObjectFromTypeLayout.

struct Params
{
    float a;
    float b;
};

ConstantBuffer<Params> gParams;

[shader("compute")]
[numthreads(1, 1, 1)]
void computeMain(uint3 tid: SV_DispatchThreadID, RWStructuredBuffer<float> resultBuffer)
{
    resultBuffer[0] = gParams.a + gParams.b;
}
