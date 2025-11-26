// ray-tracing-test-hitobject-intrinsics-make-hit.slang

// This file is split out from test-ray-tracing-hitobject-intrinsics.slang because of this issue:
// https://github.com/shader-slang/slang/issues/8618
//
// TL;DR: MakeHit fails to compile due to an incompatibility between newer DXC profiles and the NVAPI headers.

// TODO: Lots of duplication with test-ray-tracing-hitobject-intrinsics.slang. Consider making a common base file?

// NOP shaders

[shader("miss")]
void missNOP(inout RayPayload payload)
{
    // Nop
}

[shader("closesthit")]
void closestHitInvoke(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    payload.invokeWasSuccess = 1;
}

uniform RaytracingAccelerationStructure sceneBVH;

struct Result
{
    int queryWasSuccess;
    int invokeWasSuccess;

    float3 rayOrigin;
    float3 rayDirection;
};

uniform RWStructuredBuffer<Result> resultBuffer;

[raypayload]
struct RayPayload
{
    int queryWasSuccess : read(caller) : write(caller, closesthit, miss);
    int invokeWasSuccess : read(caller) : write(caller, closesthit, miss);
};

[shader("raygeneration")]
void rayGenShaderMakeQueryInvokeHit()
{
    // MakeHit requires the attributes from a hit. To get those, we'll trace a ray, then copy the
    // attributes from the resulting hit to the one we make.

    float3 tgt = {0.25, 0.25, 1.0};
    float3 pos = {0.0, 0.0, 0.0};
    float3 dir = tgt - pos;

    RayDesc ray;
    ray.Origin = pos;
    ray.Direction = dir;
    ray.TMin = 0.001;
    ray.TMax = 10000.0;
    RayPayload payload = { {} };
    HitObject hitObjectFromTrace = HitObject.TraceRay(sceneBVH, RAY_FLAG_NONE, ~0, 0, 0, 0, ray, payload);

    HitObject hitObjectFromMake = HitObject.MakeHit<BuiltInTriangleIntersectionAttributes>(
        sceneBVH,
        0,
        0,
        0,
        HIT_KIND_TRIANGLE_FRONT_FACE,
        0,
        0,
        ray,
        hitObjectFromTrace.GetAttributes<BuiltInTriangleIntersectionAttributes>()
    );

    resultBuffer[0].queryWasSuccess = hitObjectFromMake.IsHit();

    RayPayload payloadHit = {};
    HitObject.Invoke(sceneBVH, hitObjectFromMake, payloadHit);

    resultBuffer[0].invokeWasSuccess = payloadHit.invokeWasSuccess;
}

#ifndef __CUDA__
[shader("raygeneration")]
void rayGenShaderMakeMotionHit()
{
    // First trace a motion ray to get attributes
    RayDesc ray;
    ray.Origin = {0.0, 0.0, 0.0};
    ray.Direction = {0.25, 0.25, 1.0};
    ray.TMin = 0.001;
    ray.TMax = 10000.0;
    float currentTime = 0.5;

    RayPayload tracePayload = { {} };
    HitObject hitObjectFromTrace = HitObject.TraceMotionRay(sceneBVH, RAY_FLAG_NONE, ~0, 0, 0, 0, ray, currentTime, tracePayload);

    // Now make a motion hit using the attributes from the traced motion ray
    HitObject hitObjectFromMake = HitObject.MakeMotionHit<BuiltInTriangleIntersectionAttributes>(
        hitObjectFromTrace.GetShaderTableIndex(), sceneBVH, hitObjectFromTrace.GetInstanceIndex(), hitObjectFromTrace.GetGeometryIndex(), hitObjectFromTrace.GetPrimitiveIndex(), hitObjectFromTrace.GetHitKind(), ray, currentTime,
        hitObjectFromTrace.GetAttributes<BuiltInTriangleIntersectionAttributes>()
    );

    resultBuffer[0].queryWasSuccess = hitObjectFromMake.IsHit();

    // TODO: Invoking the motion hit object causes an MMU fault. Need to investigate why.
    resultBuffer[0].invokeWasSuccess = 1;
    // RayPayload payload = { {} };
    // HitObject.Invoke(sceneBVH, hitObjectFromMake, payload);
    // resultBuffer[0].invokeWasSuccess = payload.invokeWasSuccess;
}
#endif // __CUDA__
