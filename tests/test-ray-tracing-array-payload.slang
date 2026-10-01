// Dense fixed-array payload transport, independent of matrix-query support.
uniform RaytracingAccelerationStructure sceneBVH;
uniform RWStructuredBuffer<uint> arrayPayloadResults;

[raypayload]
struct FlatPayload
{
    float values[12] : read(caller, anyhit, closesthit, miss) : write(caller, anyhit, closesthit, miss);
};

struct PayloadBlock
{
    float2 values[2];
    int signedValues[2];
    uint unsignedValues[2];
};

[raypayload]
struct NestedPayload
{
    PayloadBlock blocks[2][2] : read(caller, anyhit, closesthit, miss) : write(caller, anyhit, closesthit, miss);
};

[shader("anyhit")]
void flatAnyHit(inout FlatPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    [unroll]
    for (uint word = 0; word < 12; ++word)
        payload.values[word] = payload.values[word] * 2.0 + float(word + 3);
}

[shader("closesthit")]
void flatClosestHit(inout FlatPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    [unroll]
    for (uint word = 0; word < 12; ++word)
        payload.values[word] = payload.values[word] * 3.0 - float(word + 1);
}

[shader("miss")]
void flatMiss(inout FlatPayload payload)
{
    [unroll]
    for (uint word = 0; word < 12; ++word)
        payload.values[word] = -payload.values[word] - 32.0 - float(word * 2);
}

[shader("anyhit")]
void nestedAnyHit(inout NestedPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    [unroll]
    for (uint row = 0; row < 2; ++row)
    {
        [unroll]
        for (uint column = 0; column < 2; ++column)
        {
            uint base = (row * 2 + column) * 8;
            [unroll]
            for (uint element = 0; element < 2; ++element)
            {
                [unroll]
                for (uint lane = 0; lane < 2; ++lane)
                {
                    uint word = base + element * 2 + lane;
                    payload.blocks[row][column].values[element][lane] = payload.blocks[row][column].values[element][lane] * 2.0 + float(word + 3);
                }
                uint signedWord = base + 4 + element;
                uint unsignedWord = base + 6 + element;
                payload.blocks[row][column].signedValues[element] = payload.blocks[row][column].signedValues[element] * 2 - int(signedWord + 5);
                payload.blocks[row][column].unsignedValues[element] = (payload.blocks[row][column].unsignedValues[element] ^ 0x01010101u) + unsignedWord * 3;
            }
        }
    }
}

[shader("closesthit")]
void nestedClosestHit(inout NestedPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    [unroll]
    for (uint row = 0; row < 2; ++row)
    {
        [unroll]
        for (uint column = 0; column < 2; ++column)
        {
            uint base = (row * 2 + column) * 8;
            [unroll]
            for (uint element = 0; element < 2; ++element)
            {
                [unroll]
                for (uint lane = 0; lane < 2; ++lane)
                {
                    uint word = base + element * 2 + lane;
                    payload.blocks[row][column].values[element][lane] = payload.blocks[row][column].values[element][lane] * 3.0 - float(word + 1);
                }
                uint signedWord = base + 4 + element;
                uint unsignedWord = base + 6 + element;
                payload.blocks[row][column].signedValues[element] = payload.blocks[row][column].signedValues[element] * 3 + int(signedWord + 9);
                payload.blocks[row][column].unsignedValues[element] = (payload.blocks[row][column].unsignedValues[element] ^ (0x00200000u + unsignedWord)) + unsignedWord * 7;
            }
        }
    }
}

[shader("miss")]
void nestedMiss(inout NestedPayload payload)
{
    [unroll]
    for (uint row = 0; row < 2; ++row)
    {
        [unroll]
        for (uint column = 0; column < 2; ++column)
        {
            uint base = (row * 2 + column) * 8;
            [unroll]
            for (uint element = 0; element < 2; ++element)
            {
                [unroll]
                for (uint lane = 0; lane < 2; ++lane)
                {
                    uint word = base + element * 2 + lane;
                    payload.blocks[row][column].values[element][lane] = -payload.blocks[row][column].values[element][lane] - 32.0 - float(word * 2);
                }
                uint signedWord = base + 4 + element;
                uint unsignedWord = base + 6 + element;
                payload.blocks[row][column].signedValues[element] = -payload.blocks[row][column].signedValues[element] + int(signedWord + 13);
                payload.blocks[row][column].unsignedValues[element] = (payload.blocks[row][column].unsignedValues[element] ^ 0xa5a5a5a5u) + unsignedWord * 5;
            }
        }
    }
}

[shader("raygeneration")]
void flatRayGen()
{
    uint rayIndex = DispatchRaysIndex().x;
    RayDesc ray;
    ray.Origin = float3(rayIndex == 0 ? 0.25 : 2.0, 0.25, 0.0);
    ray.Direction = float3(0.0, 0.0, 1.0);
    ray.TMin = 0.125;
    ray.TMax = 2.0;
    FlatPayload payload;
    [unroll]
    for (uint word = 0; word < 12; ++word)
        payload.values[word] = float(word + 1) * 0.25;
    TraceRay(sceneBVH, RAY_FLAG_FORCE_NON_OPAQUE, 0xff, 0, 0, 0, ray, payload);
    [unroll]
    for (uint word = 0; word < 12; ++word)
        arrayPayloadResults[1 + rayIndex * 12 + word] = asuint(payload.values[word]);
}

[shader("raygeneration")]
void nestedRayGen()
{
    uint rayIndex = DispatchRaysIndex().x;
    RayDesc ray;
    ray.Origin = float3(rayIndex == 0 ? 0.25 : 2.0, 0.25, 0.0);
    ray.Direction = float3(0.0, 0.0, 1.0);
    ray.TMin = 0.125;
    ray.TMax = 2.0;
    NestedPayload payload;
    [unroll]
    for (uint row = 0; row < 2; ++row)
    {
        [unroll]
        for (uint column = 0; column < 2; ++column)
        {
            uint base = (row * 2 + column) * 8;
            [unroll]
            for (uint element = 0; element < 2; ++element)
            {
                [unroll]
                for (uint lane = 0; lane < 2; ++lane)
                {
                    uint word = base + element * 2 + lane;
                    payload.blocks[row][column].values[element][lane] = float(word + 1) * 0.25;
                }
                payload.blocks[row][column].signedValues[element] = -int((base + 4 + element) * 7 + 11);
                payload.blocks[row][column].unsignedValues[element] = 0x81000000u + (base + 6 + element) * 17;
            }
        }
    }
    TraceRay(sceneBVH, RAY_FLAG_FORCE_NON_OPAQUE, 0xff, 0, 0, 0, ray, payload);
    [unroll]
    for (uint row = 0; row < 2; ++row)
    {
        [unroll]
        for (uint column = 0; column < 2; ++column)
        {
            uint base = (row * 2 + column) * 8;
            uint offset = 1 + rayIndex * 32 + base;
            [unroll]
            for (uint element = 0; element < 2; ++element)
            {
                [unroll]
                for (uint lane = 0; lane < 2; ++lane)
                    arrayPayloadResults[offset + element * 2 + lane] = asuint(payload.blocks[row][column].values[element][lane]);
                arrayPayloadResults[offset + 4 + element] = asuint(payload.blocks[row][column].signedValues[element]);
                arrayPayloadResults[offset + 6 + element] = payload.blocks[row][column].unsignedValues[element];
            }
        }
    }
}
