struct Buffers
{
    StructuredBuffer<uint> input;
    uint inputSentinel;
    RWStructuredBuffer<uint> mutableInput;
    uint mutableInputSentinel;
    ByteAddressBuffer bytes;
    uint bytesSentinel;
    RWByteAddressBuffer mutableBytes;
    uint mutableBytesSentinel;

    uint read()
    {
        return input[0] + mutableInput[0] + bytes.Load(0) + mutableBytes.Load<uint>(0) +
            inputSentinel + mutableInputSentinel + bytesSentinel + mutableBytesSentinel;
    }
}

struct Params
{
    Buffers buffers[2];
}

Buffers globals;
ParameterBlock<Params> params;
RWStructuredBuffer<uint> output;
AppendStructuredBuffer<uint> appended;
ConsumeStructuredBuffer<uint> consumed;

[shader("compute")]
[numthreads(1, 1, 1)]
void computeMain()
{
    output[0] = globals.read();
    output[1] = params.buffers[0].read();
    output[2] = params.buffers[1].read();
    output[3] = consumed.Consume();
    appended.Append(99);
}
