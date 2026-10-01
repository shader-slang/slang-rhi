uniform RaytracingAccelerationStructure sceneBVH;
uniform RWStructuredBuffer<uint> lifecycleResults;

[raypayload] struct LifecyclePayload
{
    uint value : read(caller, anyhit, closesthit, miss)
        : write(caller, anyhit, closesthit, miss);
    uint marker : read(caller)
        : write(caller, anyhit, closesthit, miss);
    uint phase : read(caller, closesthit, miss)
        : write(caller, anyhit, closesthit, miss);
};

[noinline] HitObject createLifecycleObject(uint index, inout LifecyclePayload payload)
{
    RayDesc ray;
    // Images of object origin(.25,.125,0), direction(0,0,1); the shared triangle is at z=1.
    ray.Origin = index == 0 ? float3(1.5, 2.5, 3.0) : index == 1 ? float3(9.0, -1.75, 1.0) : float3(30.0, 30.0, -1.0);
    ray.Direction = index == 1 ? float3(0.0, 0.0, 4.0) : float3(0.0, 0.0, 2.0);
    ray.TMin = 0.25;
    ray.TMax = 4.0;
    return HitObject.TraceRay(sceneBVH, RAY_FLAG_FORCE_NON_OPAQUE, 0xff, 0, 1, 0, ray, payload);
}

    [noinline] void observeLifecycleObject(inout HitObject object, uint row)
{
    uint base = 1 + row * 50;
    for (uint i = 0; i < 50; ++i)
        lifecycleResults[base + i] = 0;
    bool hit = object.IsHit();
    bool miss = object.IsMiss();
    lifecycleResults[base] = hit ? 1 : miss ? 2 : object.IsNop() ? 3 : 4;
    if (!hit && !miss)
        return;
    lifecycleResults[base + 6] = object.GetShaderTableIndex();
    lifecycleResults[base + 8] = asuint(object.GetCurrentTime());
    RayDesc ray = object.GetRayDesc();
    for (uint i = 0; i < 3; ++i)
    {
        lifecycleResults[base + 9 + i] = asuint(ray.Origin[i]);
        lifecycleResults[base + 12 + i] = asuint(ray.Direction[i]);
    }
    lifecycleResults[base + 15] = asuint(ray.TMin);
    lifecycleResults[base + 16] = asuint(ray.TMax);
    if (!hit)
        return;
    lifecycleResults[base + 1] = object.GetInstanceIndex();
    lifecycleResults[base + 2] = object.GetInstanceID();
    lifecycleResults[base + 3] = object.GetPrimitiveIndex();
    lifecycleResults[base + 4] = object.GetGeometryIndex();
    lifecycleResults[base + 5] = object.GetHitKind();
    float3 origin = object.GetObjectRayOrigin();
    float3 direction = object.GetObjectRayDirection();
    for (uint i = 0; i < 3; ++i)
    {
        lifecycleResults[base + 17 + i] = asuint(origin[i]);
        lifecycleResults[base + 20 + i] = asuint(direction[i]);
    }
    BuiltInTriangleIntersectionAttributes attributes = object.GetAttributes<BuiltInTriangleIntersectionAttributes>();
    lifecycleResults[base + 23] = asuint(attributes.barycentrics.x);
    lifecycleResults[base + 24] = asuint(attributes.barycentrics.y);
    float4x3 forward = object.GetObjectToWorld();
    float4x3 inverse = object.GetWorldToObject();
    for (uint r = 0; r < 3; ++r)
        for (uint c = 0; c < 4; ++c)
        {
            lifecycleResults[base + 25 + r * 4 + c] = asuint(forward[c][r]);
            lifecycleResults[base + 37 + r * 4 + c] = asuint(inverse[c][r]);
        }
}

[noinline] void forwardLifecycleObservation(inout HitObject object, uint row)
{
    observeLifecycleObject(object, row);
}

    [noinline] void invokeLifecycleObject(inout HitObject object, inout LifecyclePayload payload, uint invocation)
{
    HitObject.Invoke(sceneBVH, object, payload);
    uint base = 501 + invocation * 3;
    lifecycleResults[base] = payload.value;
    lifecycleResults[base + 1] = payload.marker;
    lifecycleResults[base + 2] = payload.phase;
}

[shader("raygeneration")] void lifecycleRayGen()
{
    LifecyclePayload traceA = {7, 0, 0};
    LifecyclePayload traceB = {7, 0, 0};
    LifecyclePayload traceMiss = {7, 0, 0};
    HitObject a = createLifecycleObject(0, traceA);
    HitObject b = createLifecycleObject(1, traceB);
    HitObject miss = createLifecycleObject(2, traceMiss);
    HitObject nop = HitObject.MakeNop();
    forwardLifecycleObservation(a, 0);
    forwardLifecycleObservation(b, 1);
    forwardLifecycleObservation(miss, 2);
    forwardLifecycleObservation(nop, 3);

    // Preserve the evolving payload across calls; a stale traversal payload or reset is observable.
    LifecyclePayload invoked = {50, 9, 0};
    invokeLifecycleObject(a, invoked, 0);
    invokeLifecycleObject(b, invoked, 1);
    a.SetShaderTableIndex(1);
    miss.SetShaderTableIndex(1);
    forwardLifecycleObservation(a, 4);
    forwardLifecycleObservation(b, 5);
    forwardLifecycleObservation(miss, 6);
    forwardLifecycleObservation(nop, 7);
    invokeLifecycleObject(a, invoked, 2);
    invokeLifecycleObject(b, invoked, 3);
    invokeLifecycleObject(miss, invoked, 4);
    invokeLifecycleObject(miss, invoked, 5);
    invokeLifecycleObject(nop, invoked, 6);
    forwardLifecycleObservation(a, 8);
    forwardLifecycleObservation(b, 9);

    lifecycleResults[522] = traceA.value;
    lifecycleResults[523] = traceA.marker;
    lifecycleResults[524] = traceA.phase;
    lifecycleResults[525] = traceB.value;
    lifecycleResults[526] = traceB.marker;
    lifecycleResults[527] = traceB.phase;
    lifecycleResults[528] = traceMiss.value;
    lifecycleResults[529] = traceMiss.marker;
    lifecycleResults[530] = traceMiss.phase;
    lifecycleResults[531] = 0xc001c0de;
}

    [shader("anyhit")] void lifecycleAnyHit0(
        inout LifecyclePayload payload,
        in BuiltInTriangleIntersectionAttributes attributes
    )
{
    // Idempotent if traversal invokes any-hit more than once for this primitive.
    payload.value = 1000 + InstanceIndex();
    payload.marker = 64;
    payload.phase = 1;
}

[shader("anyhit")] void lifecycleAnyHit1(
    inout LifecyclePayload payload,
    in BuiltInTriangleIntersectionAttributes attributes
)
{
    // Idempotent if traversal invokes any-hit more than once for this primitive.
    payload.value = 1000 + InstanceIndex();
    payload.marker = 64;
    payload.phase = 1;
}

    [shader("closesthit")] void lifecycleClosestHit0(
        inout LifecyclePayload payload,
        in BuiltInTriangleIntersectionAttributes attributes
    )
{
    payload.value += 100;
    payload.marker = InstanceID();
    payload.phase += 1;
}

[shader("closesthit")] void lifecycleClosestHit1(
    inout LifecyclePayload payload,
    in BuiltInTriangleIntersectionAttributes attributes
)
{
    payload.value += 200;
    payload.marker = InstanceID();
    payload.phase += 1;
}

    [shader("miss")] void lifecycleMiss0(inout LifecyclePayload payload)
{
    payload.value += 300;
    payload.marker = 0xbeef;
    payload.phase += 2;
}

[shader("miss")] void lifecycleMiss1(inout LifecyclePayload payload)
{
    payload.value += 400;
    payload.marker = 0xbeef;
    payload.phase += 2;
}
