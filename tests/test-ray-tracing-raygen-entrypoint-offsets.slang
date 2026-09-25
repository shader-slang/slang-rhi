uint bias;
RWStructuredBuffer<uint> globalOutput;

[shader("raygeneration")]
void rayGenOffsets(uniform RWStructuredBuffer<uint> output, uniform uint value)
{
    output[0] = value + bias;
    globalOutput[1] = bias;
}
