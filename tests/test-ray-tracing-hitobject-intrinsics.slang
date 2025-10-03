// ray-tracing-test-hitobject-intrinsics.slang

// NOP shaders

[shader("miss")]
void missNOP(inout RayPayload payload)
{
    // Nop
}

[shader("closesthit")]
void closestHitNOP(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    // Nop
}

[raypayload]
struct RayPayload
{
    int queryWasSuccess : read(caller) : write(caller, closesthit, miss);
    int invokeWasSuccess : read(caller) : write(caller, closesthit, miss);
};

// uniform uint2 dims;
// uniform RWTexture2D resultTexture;
uniform RaytracingAccelerationStructure sceneBVH;

struct Result
{
    int queryWasSuccess;
    int invokeWasSuccess;
};

uniform RWStructuredBuffer<Result> resultBuffer;

[shader("raygeneration")]
void rayGenShaderMakeQueryInvokeNOP()
{
    HitObject hitObjectNOP = HitObject.MakeNop();

    resultBuffer[0].queryWasSuccess = hitObjectNOP.IsNop();

    // Invoking a NOP shouldn't have an effect, but we do it anyway to make sure it doesn't crash.
    RayPayload payload = {};
    HitObject.Invoke(sceneBVH, hitObjectNOP, payload);
    resultBuffer[0].invokeWasSuccess = 1;
}

[shader("raygeneration")]
void rayGenShaderInvokeCH()
{
    float3 tgt = {0.25, 0.25, 1.0};
    float3 pos = {0.0, 0.0, 0.0};
    float3 dir = tgt - pos; 

    // Trace a ray directly at the triangle, guaranteeing a hit
    RayDesc ray;
    ray.Origin = pos;
    ray.Direction = dir;
    ray.TMin = 0.001;
    ray.TMax = 10000.0;
    RayPayload payload = { {} };
    TraceRay(sceneBVH, RAY_FLAG_NONE, ~0, 0, 0, 0, ray, payload);

    resultBuffer[0].queryWasSuccess = payload.queryWasSuccess;
    resultBuffer[0].invokeWasSuccess = payload.invokeWasSuccess;
}

[shader("closesthit")]
void closestHitMakeQueryInvokeNOP(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    HitObject hitObjectNOP = HitObject.MakeNop();

    payload.queryWasSuccess = hitObjectNOP.IsNop();

    // Invoking a NOP shouldn't have an effect, but we do it anyway to make sure it doesn't crash.
    RayPayload hitObjPayload = {};
    HitObject.Invoke(sceneBVH, hitObjectNOP, hitObjPayload);

    payload.invokeWasSuccess = 1;
}

[shader("raygeneration")]
void rayGenShaderInvokeMS()
{
    float3 tgt = {0.25, 0.25, 1.0};
    float3 pos = {0.0, 0.0, 0.0};
    float3 dir = -1.0 * (tgt - pos);

    // Trace a ray directly away from the triangle, guaranteeing a miss
    RayDesc ray;
    ray.Origin = pos;
    ray.Direction = dir;
    ray.TMin = 0.001;
    ray.TMax = 10000.0;
    RayPayload payload = { {} };
    TraceRay(sceneBVH, RAY_FLAG_NONE, ~0, 0, 0, 0, ray, payload);

    resultBuffer[0].queryWasSuccess = payload.queryWasSuccess;
    resultBuffer[0].invokeWasSuccess = payload.invokeWasSuccess;
}

[shader("miss")]
void missMakeQueryInvokeNOP(inout RayPayload payload)
{
    HitObject hitObjectNOP = HitObject.MakeNop();

    payload.queryWasSuccess = hitObjectNOP.IsNop();

    // Invoking a NOP shouldn't have an effect, but we do it anyway to make sure it doesn't crash.
    RayPayload hitObjPayload = {};
    HitObject.Invoke(sceneBVH, hitObjectNOP, hitObjPayload);

    payload.invokeWasSuccess = 1;
}

[shader("raygeneration")]
void rayGenShaderMakeQueryInvokeMiss()
{
    float3 tgt = {0.25, 0.25, 1.0};
    float3 pos = {0.0, 0.0, 0.0};
    float3 dir = -1.0 * (tgt - pos);

    // Trace a ray directly away from the triangle, guaranteeing a miss
    RayDesc ray;
    ray.Origin = pos;
    ray.Direction = dir;
    ray.TMin = 0.001;
    ray.TMax = 10000.0;
    RayPayload payload = { {} };
    HitObject hitObjectMiss = HitObject.TraceRay(sceneBVH, RAY_FLAG_NONE, ~0, 0, 0, 0, ray, payload);

    resultBuffer[0].queryWasSuccess = hitObjectMiss.IsMiss();

    RayPayload payloadMiss = {};
    HitObject.Invoke(sceneBVH, hitObjectMiss, payloadMiss);
    resultBuffer[0].invokeWasSuccess = payloadMiss.invokeWasSuccess;
}

[shader("miss")]
void missInvoke(inout RayPayload payload)
{
    payload.invokeWasSuccess = 1;
}

[shader("closesthit")]
void closestHitMakeQueryInvokeMiss(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    float3 tgt = {0.25, 0.25, 1.0};
    float3 pos = {0.0, 0.0, 0.0};
    float3 dir = -1.0 * (tgt - pos);

    // Trace a ray directly away from the triangle, guaranteeing a miss
    RayDesc ray;
    ray.Origin = pos;
    ray.Direction = dir;
    ray.TMin = 0.001;
    ray.TMax = 10000.0;
    RayPayload tracePayload = { {} };
    HitObject hitObjectMiss = HitObject.TraceRay(sceneBVH, RAY_FLAG_NONE, ~0, 0, 0, 0, ray, tracePayload);

    payload.queryWasSuccess = hitObjectMiss.IsMiss();

    RayPayload payloadMiss = {};
    HitObject.Invoke(sceneBVH, hitObjectMiss, payloadMiss);
    payload.invokeWasSuccess = payloadMiss.invokeWasSuccess;
}

[shader("miss")]
void missMakeQueryInvokeMiss(inout RayPayload payload)
{
    float3 tgt = {0.25, 0.25, 1.0};
    float3 pos = {0.0, 0.0, 0.0};
    float3 dir = -1.0 * (tgt - pos);

    // Trace a ray directly away from the triangle, guaranteeing a miss
    RayDesc ray;
    ray.Origin = pos;
    ray.Direction = dir;
    ray.TMin = 0.001;
    ray.TMax = 10000.0;
    RayPayload tracePayload = { {} };

    // Set miss shader index to 1 to invoke missInvoke instead of this miss program
    HitObject hitObjectMiss = HitObject.TraceRay(sceneBVH, RAY_FLAG_NONE, ~0, 0, 0, 1, ray, tracePayload);

    payload.queryWasSuccess = hitObjectMiss.IsMiss();

    RayPayload payloadMiss = {};
    HitObject.Invoke(sceneBVH, hitObjectMiss, payloadMiss);
    payload.invokeWasSuccess = payloadMiss.invokeWasSuccess;
}

[shader("raygeneration")]
void rayGenShaderMakeQueryInvokeHit()
{
    float3 tgt = {0.25, 0.25, 1.0};
    float3 pos = {0.0, 0.0, 0.0};
    float3 dir = tgt - pos;

    // Trace a ray directly away from the triangle, guaranteeing a miss
    RayDesc ray;
    ray.Origin = pos;
    ray.Direction = dir;
    ray.TMin = 0.001;
    ray.TMax = 10000.0;
    RayPayload payload = { {} };
    HitObject hitObjectHit = HitObject.TraceRay(sceneBVH, RAY_FLAG_NONE, ~0, 0, 0, 0, ray, payload);

    resultBuffer[0].queryWasSuccess = hitObjectHit.IsHit();

    RayPayload payloadHit = {};
    HitObject.Invoke(sceneBVH, hitObjectHit, payloadHit);
    resultBuffer[0].invokeWasSuccess = payloadHit.invokeWasSuccess;
}

[shader("closesthit")]
void closestHitInvoke(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    payload.invokeWasSuccess = 1;
}

[shader("closesthit")]
void closestHitMakeQueryInvokeHit(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    float3 tgt = {0.25, 0.25, 1.0};
    float3 pos = {0.0, 0.0, 0.0};
    float3 dir = tgt - pos;

    // Trace a ray directly away from the triangle, guaranteeing a miss
    RayDesc ray;
    ray.Origin = pos;
    ray.Direction = dir;
    ray.TMin = 0.001;
    ray.TMax = 10000.0;
    RayPayload tracePayload = { {} };
    // Add 1 to hit group index to invoke closestHitInvoke
    HitObject hitObjectHit = HitObject.TraceRay(sceneBVH, RAY_FLAG_NONE, ~0, 1, 0, 0, ray, tracePayload);

    payload.queryWasSuccess = hitObjectHit.IsHit();

    RayPayload payloadHit = {};
    HitObject.Invoke(sceneBVH, hitObjectHit, payloadHit);
    payload.invokeWasSuccess = payloadHit.invokeWasSuccess;
}

[shader("miss")]
void missMakeQueryInvokeHit(inout RayPayload payload)
{
    float3 tgt = {0.25, 0.25, 1.0};
    float3 pos = {0.0, 0.0, 0.0};
    float3 dir = tgt - pos;

    // Trace a ray directly away from the triangle, guaranteeing a miss
    RayDesc ray;
    ray.Origin = pos;
    ray.Direction = dir;
    ray.TMin = 0.001;
    ray.TMax = 10000.0;
    RayPayload tracePayload = { {} };
    // Add 1 to hit group index to invoke closestHitInvoke
    HitObject hitObjectHit = HitObject.TraceRay(sceneBVH, RAY_FLAG_NONE, ~0, 1, 0, 0, ray, tracePayload);

    payload.queryWasSuccess = hitObjectHit.IsHit();

    RayPayload payloadHit = {};
    HitObject.Invoke(sceneBVH, hitObjectHit, payloadHit);
    payload.invokeWasSuccess = payloadHit.invokeWasSuccess;
}