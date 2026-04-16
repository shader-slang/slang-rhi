struct Result
{
    float3 value;
    int isHit;
    uint hitKind;
    float rayTMin;
    float rayTCurrent;
    uint rayFlags;
    uint geometryIndex;
    float3 triangleVertices[3];
    float rayCurrentTime;
    uint instanceID;
    uint instanceIndex;
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

[raypayload]
struct RayPayloadAttribute
{
    uint hitKind : read(caller) : write(closesthit);
    float rayTMin : read(caller) : write(closesthit);
    float rayTCurrent : read(caller) : write(closesthit);
    uint rayFlags : read(caller) : write(closesthit);
    uint geometryIndex : read(caller) : write(closesthit);
    float3 triangleVertex0 : read(caller) : write(closesthit);
    float3 triangleVertex1 : read(caller) : write(closesthit);
    float3 triangleVertex2 : read(caller) : write(closesthit);
    float rayCurrentTime : read(caller) : write(closesthit);
    uint instanceID : read(caller) : write(closesthit);
    uint instanceIndex : read(caller) : write(closesthit);
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

[shader("miss")]
void missNOPAttribute(inout RayPayloadAttribute payload)
{
    // No-op miss shader for attribute tests.
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

// Closest hit shaders for attribute tests
[shader("closesthit")]
void closestHitWriteHitKind(inout RayPayloadAttribute payload, in BuiltInTriangleIntersectionAttributes attr)
{
    payload.hitKind = HitKind();
}

[shader("closesthit")]
void closestHitWriteRayTMin(inout RayPayloadAttribute payload, in BuiltInTriangleIntersectionAttributes attr)
{
    payload.rayTMin = RayTMin();
}

[shader("closesthit")]
void closestHitWriteRayTCurrent(inout RayPayloadAttribute payload, in BuiltInTriangleIntersectionAttributes attr)
{
    payload.rayTCurrent = RayTCurrent();
}

[shader("closesthit")]
void closestHitWriteRayFlags(inout RayPayloadAttribute payload, in BuiltInTriangleIntersectionAttributes attr)
{
    payload.rayFlags = RayFlags();
}

#if !defined(__CUDA__)
[shader("closesthit")]
void closestHitWriteGeometryIndex(inout RayPayloadAttribute payload, in BuiltInTriangleIntersectionAttributes attr)
{
    payload.geometryIndex = GeometryIndex();
}
#endif

#if !defined(__CUDA__) && !defined(__D3D12__)
[shader("closesthit")]
void closestHitWriteHitTriangleVertexPosition(inout RayPayloadAttribute payload, in BuiltInTriangleIntersectionAttributes attr)
{
    payload.triangleVertex0 = HitTriangleVertexPosition(0);
    payload.triangleVertex1 = HitTriangleVertexPosition(1);
    payload.triangleVertex2 = HitTriangleVertexPosition(2);
}

[shader("closesthit")]
void closestHitWriteRayCurrentTime(inout RayPayloadAttribute payload, in BuiltInTriangleIntersectionAttributes attr)
{
    payload.rayCurrentTime = RayCurrentTime();
}
#endif

[shader("closesthit")]
void closestHitWriteInstanceID(inout RayPayloadAttribute payload, in BuiltInTriangleIntersectionAttributes attr)
{
    payload.instanceID = InstanceID();
}

[shader("closesthit")]
void closestHitWriteInstanceIndex(inout RayPayloadAttribute payload, in BuiltInTriangleIntersectionAttributes attr)
{
    payload.instanceIndex = InstanceIndex();
}

// Raygen shader for attribute tests - writes all attributes to result
[shader("raygeneration")]
void rayGenShaderAttributeTest()
{
    RayPayloadAttribute payload = {};

    RayDesc ray;
    ray.Origin = kRayOriginWorld;
    ray.Direction = kTrianglePointObject - kRayOriginWorld;
    ray.TMin = 0.001;
    ray.TMax = 10000.0;
    TraceRay(sceneBVH, RAY_FLAG_FORCE_OPAQUE, ~0, 0, 0, 0, ray, payload);

    Result result = {};
    result.hitKind = payload.hitKind;
    result.rayTMin = payload.rayTMin;
    result.rayTCurrent = payload.rayTCurrent;
    result.rayFlags = payload.rayFlags;
    result.geometryIndex = payload.geometryIndex;
    result.triangleVertices[0] = payload.triangleVertex0;
    result.triangleVertices[1] = payload.triangleVertex1;
    result.triangleVertices[2] = payload.triangleVertex2;
    result.rayCurrentTime = payload.rayCurrentTime;
    result.instanceID = payload.instanceID;
    result.instanceIndex = payload.instanceIndex;
    resultBuffer[0] = result;
}

// Raygen shader for motion blur attribute tests - writes all attributes to result
#if !(defined(__CUDA__) || defined(__D3D12__))
[shader("raygeneration")]
void rayGenShaderMotionBlurAttributeTest()
{
    RayPayloadAttribute payload = {};

    RayDesc ray;
    // See SingleTriangleVertexMotionBLAS for triangle positions
    ray.Origin = float3(0.0, 0.0, 0.0);
    ray.Direction = float3(0.0, 0.25, 1.0);
    ray.TMin = 0.001;
    ray.TMax = 10000.0;
    float currentTime = 0.5; // midpoint of motion
    TraceMotionRay(sceneBVH, RAY_FLAG_NONE, ~0, 0, 0, 0, ray, currentTime, payload);

    Result result = {};
    result.rayCurrentTime = payload.rayCurrentTime;
    resultBuffer[0] = result;
}
#endif

#if !defined(__CUDA__)
// Callable shader that writes a value to the payload
[shader("callable")]
void callableWriteAttribute(inout RayPayload payload)
{
    payload.attribute = float3(1.0, 2.0, 3.0);
}

// Raygen shader for CallShader test
[shader("raygeneration")]
void rayGenShaderCallShaderTest()
{
    RayPayload payload = {};

    // Call the callable shader at index 0
    CallShader(0, payload);

    // Write result to buffer
    Result result = {};
    result.value = payload.attribute;
    resultBuffer[0] = result;
}
#endif
