[raypayload] struct Payload
{
    uint value : read(caller)
        : write(caller, closesthit, miss);
}

struct RayGenEnvironment
{
    RaytracingAccelerationStructure.Handle scene;
    RWStructuredBuffer<uint>.Handle output;
    uint bias;
}

struct HitEnvironment
{
    StructuredBuffer<uint>.Handle values;
    uint index;
}

    [shader("raygeneration")] void
    rayGen(uniform uint4 prefix, uniform RayGenEnvironment environment)
{
    uint index = DispatchRaysIndex().x;
    RayDesc ray;
    ray.Origin = index < 2 ? float3(0.25, 0.25, 0) : float3(2, 2, 0);
    ray.Direction = float3(0, 0, 1);
    ray.TMin = 0.001;
    ray.TMax = 10;
    Payload payload = {0};
    TraceRay(environment.scene, RAY_FLAG_NONE, 0xff, index % 2, 2, index % 2, ray, payload);
    environment.output[index] = prefix.w + environment.bias + payload.value;
}

[shader("closesthit")] void closestHit(
    inout Payload payload,
    BuiltInTriangleIntersectionAttributes attributes,
    uniform uint prefix,
    uniform HitEnvironment environment
)
{
    payload.value = prefix + nonuniform(environment.values)[environment.index];
}

    [shader("miss")] void missA(inout Payload payload, uniform uint2 prefix, uniform HitEnvironment environment)
{
    payload.value = prefix.y + nonuniform(environment.values)[environment.index];
}

[shader("miss")] void missB(inout Payload payload, uniform uint2 prefix, uniform HitEnvironment environment)
{
    payload.value = prefix.y + nonuniform(environment.values)[environment.index] + 1000;
}
