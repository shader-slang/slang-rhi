uniform RaytracingAccelerationStructure sceneBVH;
uniform RWStructuredBuffer<float2> resultBuffer;

struct Payload
{
    float distance;
};

[shader("raygeneration")]
void raygenGraphPolicy()
{
    RayDesc ray;
    ray.Origin = float3(0.25, 0.25, 0.0);
    ray.Direction = float3(0.0, 0.0, 1.0);
    ray.TMin = 0.0;
    ray.TMax = 10.0;
    Payload hit = {0.0};
    TraceRay(sceneBVH, RAY_FLAG_NONE, 0xff, 0, 0, 0, ray, hit);
    ray.Origin = float3(2.0, 2.0, 0.0);
    Payload miss = {0.0};
    TraceRay(sceneBVH, RAY_FLAG_NONE, 0xff, 0, 0, 0, ray, miss);
    resultBuffer[0] = float2(hit.distance, miss.distance);
}

[shader("closesthit")]
void closestHitGraphPolicy(inout Payload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    payload.distance = RayTCurrent();
}

[shader("miss")]
void missGraphPolicy(inout Payload payload)
{
    payload.distance = -1.0;
}
