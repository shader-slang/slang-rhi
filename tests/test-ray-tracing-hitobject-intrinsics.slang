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

uniform RaytracingAccelerationStructure sceneBVH;

struct Result
{
    int queryWasSuccess;
    int invokeWasSuccess;

    float3 rayOrigin;
    float3 rayDirection;
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
void rayGenShaderTraceQueryInvokeHit()
{
    float3 tgt = {0.25, 0.25, 1.0};
    float3 pos = {0.0, 0.0, 0.0};
    float3 dir = tgt - pos;

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

[shader("raygeneration")]
void rayGenShaderQueryHitKindFrontFace()
{
    float3 tgt = {0.25, 0.25, 0.0};
    float3 pos = {0.0, 0.0, 2.0};
    float3 dir = tgt - pos;

    RayDesc ray;
    ray.Origin = pos;
    ray.Direction = dir;
    ray.TMin = 0.001;
    ray.TMax = 10000.0;
    RayPayload payload = { {} };
    HitObject hitObjectHit = HitObject.TraceRay(sceneBVH, RAY_FLAG_NONE, ~0, 0, 0, 0, ray, payload);

#if (defined(__D3D12__) || defined(__VULKAN__))
  resultBuffer[0].queryWasSuccess = hitObjectHit.GetHitKind() == HIT_KIND_TRIANGLE_FRONT_FACE;
#elif defined(__CUDA__)
  // TODO: GetHitKind not implemented for OptiX target
  resultBuffer[0].queryWasSuccess = 0;
#else
  resultBuffer[0].queryWasSuccess = 0;
#endif

    // We don't need to invoke the hit object for this test
    resultBuffer[0].invokeWasSuccess = 1;
}

// TODO: Slang fails with an internal error when an empty struct is passed to ReportHit
struct HitKindAttributes { int dummy; };

[shader("raygeneration")]
void rayGenShaderQueryHitKindBackFace()
{
    float3 tgt = {0.25, 0.25, 1.0};
    float3 pos = {0.0, 0.0, 0.0};
    float3 dir = tgt - pos;

    RayDesc ray;
    ray.Origin = pos;
    ray.Direction = dir;
    ray.TMin = 0.001;
    ray.TMax = 10000.0;
    RayPayload payload = { {} };
    HitObject hitObjectHit = HitObject.TraceRay(sceneBVH, RAY_FLAG_NONE, ~0, 0, 0, 0, ray, payload);

#if (defined(__D3D12__) || defined(__VULKAN__))
  resultBuffer[0].queryWasSuccess = hitObjectHit.GetHitKind() == HIT_KIND_TRIANGLE_BACK_FACE;
#elif defined(__CUDA__)
  // TODO: GetHitKind not implemented for OptiX target
  resultBuffer[0].queryWasSuccess = 0;
#else
  resultBuffer[0].queryWasSuccess = 0;
#endif

    // We don't need to invoke the hit object for this test
    resultBuffer[0].invokeWasSuccess = 1;
}

static const uint CUSTOM_HIT_KIND = 13;

[shader("raygeneration")]
void rayGenShaderQueryHitKindCustom()
{
    float3 tgt = {0.0, 0.0, 1.0};
    float3 pos = {0.0, 0.0, 0.0};
    float3 dir = tgt - pos;

    RayDesc ray;
    ray.Origin = pos;
    ray.Direction = dir;
    ray.TMin = 0.001;
    ray.TMax = 10000.0;
    RayPayload payload = { {} };
    HitObject hitObjectHit = HitObject.TraceRay(sceneBVH, RAY_FLAG_NONE, ~0, 0, 0, 0, ray, payload);

#if (defined(__D3D12__) || defined(__VULKAN__))
  resultBuffer[0].queryWasSuccess = hitObjectHit.GetHitKind() == CUSTOM_HIT_KIND;
#elif defined(__CUDA__)
  // TODO: GetHitKind not implemented for OptiX target
  resultBuffer[0].queryWasSuccess = 0;
#else
  resultBuffer[0].queryWasSuccess = 0;
#endif

    // We don't need to invoke the hit object for this test
    resultBuffer[0].invokeWasSuccess = 1;
}

[shader("intersection")]
void intersectionReportHitWithKind()
{
#if defined(__CUDA__)
    ReportHitOptix( 1.0, CUSTOM_HIT_KIND, 0 );
#else
    HitKindAttributes h = { 0 };
    ReportHit( 1.0, CUSTOM_HIT_KIND, h );
#endif
}

#if !(defined(__CUDA__) || defined(__D3D12__))
[shader("raygeneration")]
void rayGenShaderQueryRayObjectOrigin()
{
    // We hand-specify these so we can check them on the host
    float3 origin = {0.1, 0.1, 0.1};
    float3 dir = {0.0, 0.0, 1.0};

    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = dir;
    ray.TMin = 0.001;
    ray.TMax = 10000.0;
    RayPayload payload = { {} };
    HitObject hitobj = HitObject.TraceRay(sceneBVH, RAY_FLAG_NONE, ~0, 0, 0, 0, ray, payload);

    resultBuffer[0].rayOrigin = hitobj.GetObjectRayOrigin();
}
#endif

#if !(defined(__CUDA__) || defined(__D3D12__))
[shader("raygeneration")]
void rayGenShaderQueryRayObjectDirection()
{
    // We hand-specify these so we can check them on the host
    float3 origin = {0.1, 0.1, 0.1};
    float3 dir = {0.0, 0.0, 1.0};

    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = dir;
    ray.TMin = 0.001;
    ray.TMax = 10000.0;
    RayPayload payload = { {} };
    HitObject hitobj = HitObject.TraceRay(sceneBVH, RAY_FLAG_NONE, ~0, 0, 0, 0, ray, payload);

    resultBuffer[0].rayDirection = hitobj.GetObjectRayDirection();
}
#endif

[shader("raygeneration")]
void rayGenShaderMakeMiss()
{
    // Create a miss hit object using MakeMiss
    RayDesc ray;
    ray.Origin = {0.0, 0.0, 0.0};
    ray.Direction = {0.0, 0.0, -1.0};  // away from geometry
    ray.TMin = 0.001;
    ray.TMax = 10000.0;

    HitObject hitObjectMiss = HitObject.MakeMiss(0, ray);

    resultBuffer[0].queryWasSuccess = hitObjectMiss.IsMiss();

    RayPayload payload = {};
    HitObject.Invoke(sceneBVH, hitObjectMiss, payload);
    resultBuffer[0].invokeWasSuccess = payload.invokeWasSuccess;
}

// NOTE: We disable this code under D3D12 because it causes compilation to fail, but it's not clear
// if it is failing due to a bug in the test or a bug in Slang. Revisit this if/when motion blur
// support is added to the D3D12 backend.
#if !defined(__D3D12__)
[shader("raygeneration")]
void rayGenShaderMakeMotionMiss()
{
    RayDesc ray;
    ray.Origin = {0.0, 0.0, 0.0};
    ray.Direction = {0.0, 0.0, -1.0};
    ray.TMin = 0.001;
    ray.TMax = 10000.0;

    float currentTime = 0.5;  // midpoint of motion
    HitObject hitObjectMiss = HitObject.MakeMotionMiss(0, ray, currentTime);

    const bool isMiss = hitObjectMiss.IsMiss();

    // TODO: Enable this when https://github.com/shader-slang/slang/issues/9018 is fixed
    // const bool timeIsCorrect = hitObjectMiss.GetCurrentTime() == currentTime;

    resultBuffer[0].queryWasSuccess = isMiss/* && timeIsCorrect*/;

    RayPayload payload = {};
    HitObject.Invoke(sceneBVH, hitObjectMiss, payload);
    resultBuffer[0].invokeWasSuccess = payload.invokeWasSuccess;
}
#endif

[shader("raygeneration")]
void rayGenShaderTraceMotionRay()
{
    float3 origin = {-0.5, 0.5, 0.0};
    float3 dir = {0.0, 0.0, 1.0};

    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = dir;
    ray.TMin = 0.001;
    ray.TMax = 10000.0;
    float currentTime = 1.0;
    RayPayload payload = { {} };
    HitObject hitobj = HitObject.TraceMotionRay(sceneBVH, RAY_FLAG_NONE, ~0, 0, 0, 0, ray, currentTime, payload);

    // Check if it's a hit
    resultBuffer[0].queryWasSuccess = hitobj.IsHit() ? 1 : 0;

    // Invoke the hit
    RayPayload payloadInvoke = {};
    HitObject.Invoke(sceneBVH, hitobj, payloadInvoke);
    resultBuffer[0].invokeWasSuccess = payloadInvoke.invokeWasSuccess;
}
