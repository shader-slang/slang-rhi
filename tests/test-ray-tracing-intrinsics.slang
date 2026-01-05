struct Result
{
    float3 value;
    int isHit;
};

uniform RaytracingAccelerationStructure sceneBVH;
uniform RWStructuredBuffer<Result> resultBuffer;

[raypayload]
struct RayPayload
{
    float3 attribute : read(caller) : write(closesthit);
};

[raypayload]
struct HitTestPayload
{
    int isHit : read(caller) : write(caller, closesthit, anyhit, miss);
};

static const float3 kInstanceTranslation = float3(1.0, 2.0, 3.0);
static const float3 kRayOriginWorld = float3(0.0, 0.0, 0.0);
static const float3 kTrianglePointObject = float3(0.25, 0.25, 1.0);

[shader("closesthit")]
void closestHitNOP(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    // NOP
}

// OptiX only allows calling ObjectRayOrigin from any hit or intersection.
#if !defined(__CUDA__)
[shader("closesthit")]
void closestHitWriteObjectRayOrigin(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    payload.attribute = ObjectRayOrigin();
}
#endif

[shader("anyhit")]
void anyHitWriteObjectRayOrigin(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    payload.attribute = ObjectRayOrigin();
}

[shader("closesthit")]
void closestHitWriteWorldRayOrigin(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    payload.attribute = WorldRayOrigin();
}

// OptiX only allows calling ObjectRayDirection from any hit or intersection.
#if !defined(__CUDA__)
[shader("closesthit")]
void closestHitWriteObjectRayDirection(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    payload.attribute = ObjectRayDirection();
}
#endif

[shader("anyhit")]
void anyHitWriteObjectRayDirection(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    payload.attribute = ObjectRayDirection();
}

[shader("closesthit")]
void closestHitWriteWorldRayDirection(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    payload.attribute = WorldRayDirection();
}

[shader("miss")]
void missNOP(inout RayPayload payload)
{
    // No-op miss shader.
}

[shader("raygeneration")]
void rayGenShaderObjectRayOrigin()
{
    RayPayload payload = {};

    RayDesc ray;
    ray.Origin = kRayOriginWorld;
    ray.Direction = (kTrianglePointObject + kInstanceTranslation) - kRayOriginWorld;
    ray.TMin = 0.001;
    ray.TMax = 10000.0;
    TraceRay(sceneBVH, RAY_FLAG_NONE, ~0, 0, 0, 0, ray, payload);

    Result result = {};
    result.value = payload.attribute;
    resultBuffer[0] = result;
}

[shader("raygeneration")]
void rayGenShaderWorldRayOrigin()
{
    RayPayload payload = {};

    RayDesc ray;
    ray.Origin = kRayOriginWorld;
    ray.Direction = (kTrianglePointObject + kInstanceTranslation) - kRayOriginWorld;
    ray.TMin = 0.001;
    ray.TMax = 10000.0;
    TraceRay(sceneBVH, RAY_FLAG_NONE, ~0, 0, 0, 0, ray, payload);

    Result result = {};
    result.value = payload.attribute;
    resultBuffer[0] = result;
}

[shader("raygeneration")]
void rayGenShaderObjectRayDirection()
{
    RayPayload payload = {};

    RayDesc ray;
    ray.Origin = kRayOriginWorld;
    ray.Direction = (kTrianglePointObject + kInstanceTranslation) - kRayOriginWorld;
    ray.TMin = 0.001;
    ray.TMax = 10000.0;
    TraceRay(sceneBVH, RAY_FLAG_NONE, ~0, 0, 0, 0, ray, payload);

    Result result = {};
    result.value = payload.attribute;
    resultBuffer[0] = result;
}

[shader("raygeneration")]
void rayGenShaderWorldRayDirection()
{
    RayPayload payload = {};

    RayDesc ray;
    ray.Origin = kRayOriginWorld;
    ray.Direction = (kTrianglePointObject + kInstanceTranslation) - kRayOriginWorld;
    ray.TMin = 0.001;
    ray.TMax = 10000.0;
    TraceRay(sceneBVH, RAY_FLAG_NONE, ~0, 0, 0, 0, ray, payload);

    Result result = {};
    result.value = payload.attribute;
    resultBuffer[0] = result;
}

// Anyhit shader that accepts and ends search
[shader("anyhit")]
void anyhitAcceptAndEnd(inout HitTestPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    AcceptHitAndEndSearch();
}

// Anyhit shader that ignores the hit
[shader("anyhit")]
void anyhitIgnore(inout HitTestPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    IgnoreHit();
}

// Closesthit shader for anyhit tests
[shader("closesthit")]
void closestHitSetHit(inout HitTestPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    payload.isHit = 1;
}

// Raygen shader for anyhit tests
[shader("raygeneration")]
void rayGenShaderAnyhitTest()
{
    HitTestPayload payload = {};
    payload.isHit = 0; // Initialize to not hit

    RayDesc ray;
    ray.Origin = kRayOriginWorld;
    ray.Direction = kTrianglePointObject - kRayOriginWorld; // No transform, hits untransformed triangle
    ray.TMin = 0.001;
    ray.TMax = 10000.0;
    TraceRay(sceneBVH, RAY_FLAG_NONE, ~0, 0, 0, 0, ray, payload);

    Result result = {};
    result.isHit = payload.isHit;
    resultBuffer[0] = result;
}
