[raypayload]
struct Payload
{
    float distance : read(caller) : write(caller, closesthit, miss);
};

uniform RaytracingAccelerationStructure currentScene;
uniform RaytracingAccelerationStructure previousScene;
uniform RWStructuredBuffer<float2> results;

[shader("raygeneration")]
void rayGen()
{
    uint index = DispatchRaysIndex().x;
    RayDesc ray;
    ray.Origin = float3(0.25 + 2.0 * index, 0.25, 0.0);
    ray.Direction = float3(0.0, 0.0, 1.0);
    ray.TMin = 0.0;
    ray.TMax = 10.0;

    Payload current = {-1.0};
    Payload previous = {-1.0};
    TraceRay(currentScene, RAY_FLAG_NONE, 0xff, 0, 1, 0, ray, current);
    TraceRay(previousScene, RAY_FLAG_NONE, 0xff, 0, 1, 0, ray, previous);
    results[index] = float2(current.distance, previous.distance);
}

[shader("closesthit")]
void closestHit(inout Payload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    payload.distance = RayTCurrent();
}

[shader("miss")]
void miss(inout Payload payload)
{
    payload.distance = -1.0;
}
