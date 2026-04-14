// test-parallel-pipeline-creation-rt.slang
// Ray tracing entry points for testing parallel RT pipeline creation.

uniform RaytracingAccelerationStructure sceneBVH;
uniform RWStructuredBuffer<float> rtResultBuffer;

[raypayload]
struct RayPayload
{
    float value : read(caller) : write(caller, closesthit, miss);
};

[shader("raygeneration")]
void rayGenShader()
{
    RayDesc ray;
    ray.Origin = float3(0.5, 0.5, 0.0);
    ray.Direction = float3(0, 0, 1);
    ray.TMin = 0.001;
    ray.TMax = 10000.0;
    RayPayload payload = {0.0};
    TraceRay(sceneBVH, RAY_FLAG_NONE, ~0, 0, 0, 0, ray, payload);
    rtResultBuffer[0] = payload.value;
}

[shader("closesthit")]
void closestHitShader(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    payload.value = 1.0;
}

[shader("miss")]
void missShader(inout RayPayload payload)
{
    payload.value = -1.0;
}
