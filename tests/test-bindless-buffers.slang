uniform Buffer<float>.Handle buffer;
uniform StructuredBuffer<float>.Handle structuredBuffer;
uniform ByteAddressBuffer.Handle byteAddressBuffer;

uniform RWBuffer<float>.Handle rwBuffer;
uniform RWStructuredBuffer<float>.Handle rwStructuredBuffer;
uniform RWByteAddressBuffer.Handle rwByteAddressBuffer;

uniform RWStructuredBuffer<float> result;

[shader("compute")]
[numthreads(1, 1, 1)]
void computeMain()
{
    uint index = 0;

#ifndef __CUDA__
    // Buffer
    {
        result[index++] = buffer[0];
        result[index++] = buffer[1];
    }
#else
    result[index++] = 1.0;
    result[index++] = 2.0;
#endif

    // StructuredBuffer
    {
        result[index++] = structuredBuffer[0];
        result[index++] = structuredBuffer[1];
    }

    // ByteAddressBuffer
    {
        result[index++] = byteAddressBuffer.Load<float>(0);
        result[index++] = byteAddressBuffer.Load<float>(4);
    }

#ifndef __CUDA__
    // RWBuffer
    {
        result[index++] = rwBuffer[0];
        result[index++] = rwBuffer[1];
        rwBuffer[0] += 1;
        rwBuffer[1] += 1;
    }
#else
    result[index++] = 1.0;
    result[index++] = 2.0;
#endif

    // RWStructuredBuffer
    {
        result[index++] = rwStructuredBuffer[0];
        result[index++] = rwStructuredBuffer[1];
        rwStructuredBuffer[0] += 1;
        rwStructuredBuffer[1] += 1;
    }

    // RWByteAddressBuffer
    {
        result[index++] = rwByteAddressBuffer.Load<float>(0);
        result[index++] = rwByteAddressBuffer.Load<float>(4);
        rwByteAddressBuffer.Store<float>(0, rwByteAddressBuffer.Load<float>(0) + 1);
        rwByteAddressBuffer.Store<float>(4, rwByteAddressBuffer.Load<float>(4) + 1);
    }
}
