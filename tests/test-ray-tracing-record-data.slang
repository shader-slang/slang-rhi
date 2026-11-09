[raypayload] struct Payload
{
    uint value : read(caller)
        : write(caller, closesthit, miss);
}

RaytracingAccelerationStructure scene;
StructuredBuffer<uint> arena;
RWStructuredBuffer<uint> output;

[shader("raygeneration")] void rayGen(uniform uint environment)
{
    uint index = DispatchRaysIndex().x;
    RayDesc ray;
    ray.Origin = index < 2 ? float3(0.25, 0.25, 0) : float3(2, 2, 0);
    ray.Direction = float3(0, 0, 1);
    ray.TMin = 0.001;
    ray.TMax = 10;
    Payload payload = {0};
    TraceRay(scene, RAY_FLAG_NONE, 0xff, index % 2, 2, index % 2, ray, payload);
    output[index] = arena[environment] + payload.value;
}

    [shader("closesthit")] void closestHit(
        inout Payload payload,
        BuiltInTriangleIntersectionAttributes attributes,
        uniform uint4 environment
    )
{
    payload.value = arena[environment.w] + environment.x + environment.y + environment.z;
}

[shader("miss")] void missA(inout Payload payload, uniform uint2 environment)
{
    payload.value = arena[environment.y] + environment.x;
}

    [shader("miss")] void missB(inout Payload payload, uniform uint2 environment)
{
    payload.value = arena[environment.y] + environment.x + 1000;
}
