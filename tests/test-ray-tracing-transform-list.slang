// Query a single static instance path; no transform-matrix or motion semantics are assumed.
uniform RaytracingAccelerationStructure sceneBVH;
uniform RWStructuredBuffer<uint> transformListResults;
uniform uint transformListIndex;

struct TransformObservation
{
    uint facts[4];
    uint handles[4];
};

[raypayload]
struct TransformListPayload
{
    TransformObservation anyHit : read(caller) : write(caller, anyhit);
    TransformObservation closestHit : read(caller) : write(caller, closesthit);
    uint phase : read(caller, closesthit) : write(caller, anyhit, closesthit, miss);
};

TransformObservation observeTransformList()
{
    // The uniform index deliberately remains dynamic even though this scene supplies zero.
    uint64_t handle = GetTransformListHandle(transformListIndex);
    uint64_t child = GetTraversableChild(handle);
    TransformObservation result;
    result.facts[0] = GetTransformListSize();
    result.facts[1] = transformListIndex;
    result.facts[2] = uint(GetTraversableTransformType(handle));
    result.facts[3] = GetTraversableInstanceId(handle);
    result.handles[0] = uint(handle);
    result.handles[1] = uint(handle >> 32);
    result.handles[2] = uint(child);
    result.handles[3] = uint(child >> 32);
    return result;
}

[shader("anyhit")]
void transformListAnyHit(inout TransformListPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    // Repeated invocations for this same candidate only repeat these idempotent observations.
    payload.anyHit = observeTransformList();
    payload.phase = 1;
}

[shader("closesthit")]
void transformListClosestHit(inout TransformListPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    payload.closestHit = observeTransformList();
    payload.phase |= 2;
}

[shader("miss")]
void transformListMiss(inout TransformListPayload payload)
{
    payload.phase = 4;
}

[shader("raygeneration")]
void transformListRayGen()
{
    uint rayIndex = DispatchRaysIndex().x;
    float x = rayIndex == 0 ? 0.25 : rayIndex == 1 ? 4.25 : 10.0;
    RayDesc ray;
    ray.Origin = float3(x, 0.25, 0.0);
    ray.Direction = float3(0.0, 0.0, 1.0);
    ray.TMin = 0.25;
    ray.TMax = 2.0;
    TransformListPayload payload = {};
    [unroll]
    for (uint lane = 0; lane < 4; ++lane)
    {
        payload.anyHit.facts[lane] = 0xdead0000 + lane;
        payload.anyHit.handles[lane] = 0xdead0004 + lane;
        payload.closestHit.facts[lane] = 0xdead0008 + lane;
        payload.closestHit.handles[lane] = 0xdead000c + lane;
    }
    TraceRay(sceneBVH, RAY_FLAG_FORCE_NON_OPAQUE, 0xff, 0, 0, 0, ray, payload);
    uint offset = 1 + rayIndex * 17;
    [unroll]
    for (uint lane = 0; lane < 4; ++lane)
    {
        transformListResults[offset + lane] = payload.anyHit.facts[lane];
        transformListResults[offset + 4 + lane] = payload.anyHit.handles[lane];
        transformListResults[offset + 8 + lane] = payload.closestHit.facts[lane];
        transformListResults[offset + 12 + lane] = payload.closestHit.handles[lane];
    }
    transformListResults[offset + 16] = payload.phase;
}
