[raypayload] struct Payload
{
    float distance : read(caller)
        : write(caller, closesthit, miss);
};

uniform RaytracingAccelerationStructure scene;
uniform RWStructuredBuffer<float> results;

[shader("raygeneration")] void rayGen()
{
    uint index = DispatchRaysIndex().x;
    RayDesc ray;
    ray.Origin = float3(0.25 + 2.0 * index, 0.25, 0.0);
    ray.Direction = float3(0.0, 0.0, 1.0);
    ray.TMin = 0.0;
    ray.TMax = 10.0;
    Payload payload = {-1.0};
    TraceRay(scene, RAY_FLAG_NONE, 0xff, 0, 1, 0, ray, payload);
    results[index] = payload.distance;
}

    [shader("closesthit")] void closestHit(inout Payload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    payload.distance = RayTCurrent();
}

[shader("miss")] void miss(inout Payload payload)
{
    payload.distance = -1.0;
}

