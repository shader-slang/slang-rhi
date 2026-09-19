#include "test-benchmark-bindings-common.slang"
RWStructuredBuffer<uint> output;
RaytracingAccelerationStructure scene;
[raypayload]
struct Payload
{
    uint value : read(caller) : write(caller, closesthit, miss);
};
[shader("raygeneration")]
void rayGen()
{
    RayDesc ray;
    ray.Origin = float3((iteration & 1) != 0 ? 2.0 : 0.25, 0.25, 0.0);
    ray.Direction = float3(0, 0, 1);
    ray.TMin = 0.001;
    ray.TMax = 10.0;
    Payload p = {0};
    TraceRay(scene, RAY_FLAG_NONE, 0xff, 0, 0, 0, ray, p);
    output[0] += bindingValue() + p.value;
}
[shader("closesthit")]
void closestHit(inout Payload p, BuiltInTriangleIntersectionAttributes attributes)
{
    p.value = 2;
}
[shader("miss")]
void miss(inout Payload p)
{
    p.value = 3;
}
