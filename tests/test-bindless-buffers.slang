struct Params
{
    Buffer<float>.Handle buffer;
    StructuredBuffer<float>.Handle structuredBuffer;
    ByteAddressBuffer.Handle byteAddressBuffer;
    RWBuffer<float>.Handle rwBuffer;
    RWStructuredBuffer<float>.Handle rwStructuredBuffer;
    RWByteAddressBuffer.Handle rwByteAddressBuffer;
};

ParameterBlock<Params> params;

uniform RWStructuredBuffer<float> result;

[shader("compute")]
[numthreads(1, 1, 1)]
void computeMain()
{
    uint index = 0;

    // Buffer
    {
        result[index++] = params.buffer[0];
        result[index++] = params.buffer[1];
    }

    // StructuredBuffer
    {
        result[index++] = params.structuredBuffer[0];
        result[index++] = params.structuredBuffer[1];
    }

    // ByteAddressBuffer
    {
        result[index++] = params.byteAddressBuffer.Load<float>(0);
        result[index++] = params.byteAddressBuffer.Load<float>(4);
    }

    // RWBuffer
    {
        result[index++] = params.rwBuffer[0];
        result[index++] = params.rwBuffer[1];
        params.rwBuffer[0] += 1;
        params.rwBuffer[1] += 1;
    }

    // RWStructuredBuffer
    {
        result[index++] = params.rwStructuredBuffer[0];
        result[index++] = params.rwStructuredBuffer[1];
        params.rwStructuredBuffer[0] += 1;
        params.rwStructuredBuffer[1] += 1;
    }

    // RWByteAddressBuffer
    {
        result[index++] = params.rwByteAddressBuffer.Load<float>(0);
        result[index++] = params.rwByteAddressBuffer.Load<float>(4);
        params.rwByteAddressBuffer.Store<float>(0, params.rwByteAddressBuffer.Load<float>(0) + 1);
        params.rwByteAddressBuffer.Store<float>(4, params.rwByteAddressBuffer.Load<float>(4) + 1);
    }
}
