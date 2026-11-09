// Different reflected sizes exercise independent SBT section strides. Vulkan uses a full
// 4096-byte record; D3D12's largest uniform block reaches its temporary 512-byte driver cap.
[raypayload] struct Payload
{
    uint value : read(caller)
        : write(caller, closesthit, miss);
}

struct CallableData
{
    uint value;
}

RaytracingAccelerationStructure scene;
RWStructuredBuffer<uint> output;

uint sumRecord<let N : int>(uint4 environment[N])
{
    uint sum = 0;
    for (uint i = 0; i < N; ++i)
        sum += environment[i].x + environment[i].y + environment[i].z + environment[i].w;
    return sum;
}

[shader("raygeneration")] void rayGen(uniform uint4 environment[20])
{
    uint index = DispatchRaysIndex().x;
    RayDesc ray;
    ray.Origin = index < 2 ? float3(0.25, 0.25, 0) : float3(2, 2, 0);
    ray.Direction = float3(0, 0, 1);
    ray.TMin = 0.001;
    ray.TMax = 10;
    Payload payload = {0};
    TraceRay(scene, RAY_FLAG_NONE, 0xff, index % 2, 2, index % 2, ray, payload);
    CallableData callableData = {0};
    CallShader(index % 2, callableData);
    output[index] = sumRecord(environment) + payload.value + callableData.value;
}

    [shader("closesthit")] void closestHit(
        inout Payload payload,
        BuiltInTriangleIntersectionAttributes attributes,
        uniform uint4 environment[24]
    )
{
    payload.value = sumRecord(environment);
}

[shader("miss")] void missLarge(inout Payload payload, uniform uint4 environment[254])
{
    payload.value = sumRecord(environment);
}

    [shader("miss")] void missLimited(inout Payload payload, uniform uint4 environment[32])
{
    payload.value = sumRecord(environment);
}

[shader("miss")] void missSmall(inout Payload payload, uniform uint4 environment[4])
{
    payload.value = sumRecord(environment);
}

    [shader("callable")] void callableMain(inout CallableData data, uniform uint4 environment[28])
{
    data.value = sumRecord(environment);
}
