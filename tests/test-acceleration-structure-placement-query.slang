uniform RaytracingAccelerationStructure scene;
uniform RWStructuredBuffer<float> results;

[shader("compute")]
[numthreads(2, 1, 1)]
void traceInline(uint3 id : SV_DispatchThreadID)
{
    RayDesc ray;
    ray.Origin = float3(0.25 + 2.0 * id.x, 0.25, 0.0);
    ray.Direction = float3(0.0, 0.0, 1.0);
    ray.TMin = 0.0;
    ray.TMax = 10.0;
    RayQuery<RAY_FLAG_FORCE_OPAQUE> query;
    query.TraceRayInline(scene, RAY_FLAG_NONE, 0xff, ray);
    while (query.Proceed())
    {}
    results[id.x] = query.CommittedStatus() == COMMITTED_TRIANGLE_HIT ? query.CommittedRayT() : -1.0;
}
