[raypayload]
struct Payload
{
    uint result : read(caller) : write(caller, closesthit, miss);
};

uniform RWStructuredBuffer<uint> resultBuffer;
uniform RaytracingAccelerationStructure sceneBVH;

[shader("raygeneration")]
void rayGen()
{
    uint rayIndex = DispatchRaysIndex().x;

    static const float2 origins[] =
    {
        float2(-0.5, 0.0), // Fully transparent triangle.
        float2(0.5, 0.0),  // Fully opaque triangle.
        float2(1.25, -1.0 / 3.0), // Partial triangle: microtriangle 0.
        float2(1.5, -1.0 / 6.0),  // Partial triangle: microtriangle 1.
        float2(1.75, -1.0 / 3.0), // Partial triangle: microtriangle 2.
        float2(1.5, 1.0 / 6.0),   // Partial triangle: microtriangle 3.
    };

    RayDesc ray;
    ray.Origin = float3(origins[rayIndex], 0.0);
    ray.Direction = float3(0.0, 0.0, 1.0);
    ray.TMin = 0.001;
    ray.TMax = 100.0;

    Payload payload = {0};
    TraceRay(sceneBVH, RAY_FLAG_NONE, 0xff, 0, 0, 0, ray, payload);
    resultBuffer[rayIndex] = payload.result;
}

[shader("miss")]
void miss(inout Payload payload)
{
    payload.result = 1;
}

[shader("closesthit")]
void closestHit(inout Payload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    payload.result = 2;
}
