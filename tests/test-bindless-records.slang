struct Record
{
    StructuredBuffer<float>.Handle buffer;
    Buffer<float>.Handle typedBuffer;
    RWBuffer<float>.Handle writableBuffer;
    Texture2DArray<float>.Handle texture;
    SamplerState.Handle sampler;
};

StructuredBuffer<Record> records;
RWStructuredBuffer<float> result;

float readRecord(uint index)
{
    Record record = records[index];
    return record.buffer[0] + record.typedBuffer[1] +
        record.texture.SampleLevel(record.sampler, float3(0.5, 0.5, 0), 0);
}

[shader("compute")]
[numthreads(1, 1, 1)]
void computeMain(uint3 tid : SV_DispatchThreadID)
{
    result[tid.x] = readRecord(tid.x);
    records[tid.x].writableBuffer[0] += 1;
}

[shader("vertex")]
float4 vertexMain(uint vid : SV_VertexID) : SV_Position
{
    float2 uv = float2((vid << 1) & 2, vid & 2);
    // Exercise a buffer handle in the vertex stage as well as the fragment stage.
    float scale = records[0].buffer[0] / 3.0;
    return float4((uv * float2(2, -2) + float2(-1, 1)) * scale, 0, 1);
}

[shader("fragment")]
float4 fragmentMain(float4 position : SV_Position) : SV_Target
{
    return float4(readRecord(uint(position.x)), 0, 0, 1);
}
