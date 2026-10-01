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
    float3 attribute : read(caller) : write(closesthit, anyhit);
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
// The object-ray tests use their separate swapped/scaled instance transform.
static const float3 kObjectTestTargetWorld = float3(0.5, 3.0, 11.0);

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
    ray.Direction = kObjectTestTargetWorld - kRayOriginWorld;
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
    ray.Direction = kObjectTestTargetWorld - kRayOriginWorld;
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

// A separate binding keeps the existing Result layout and attribute tests unchanged.
uniform RWStructuredBuffer<uint> identityResults;

[raypayload] struct IdentityPayload
{
    uint primitive : read(caller)
        : write(closesthit, miss);
    uint instanceIndex : read(caller)
        : write(closesthit, miss);
    uint instanceID : read(caller)
        : write(closesthit, miss);
    uint hit : read(caller)
        : write(closesthit, miss);
    uint rayFlags : read(caller)
        : write(closesthit, miss);
    uint hitKind : read(caller)
        : write(closesthit, miss);
};

[shader("closesthit")] void closestHitWriteIdentities(
    inout IdentityPayload payload,
    in BuiltInTriangleIntersectionAttributes attr
)
{
    payload.primitive = PrimitiveIndex();
    payload.instanceIndex = InstanceIndex();
    payload.instanceID = InstanceID();
    payload.hit = 1;
    payload.rayFlags = RayFlags();
    payload.hitKind = HitKind();
}

    [shader("miss")] void missWriteIdentities(inout IdentityPayload payload)
{
    payload.primitive = 0xdead0001;
    payload.instanceIndex = 0xdead0002;
    payload.instanceID = 0xdead0003;
    payload.hit = 0;
    payload.rayFlags = RayFlags();
    payload.hitKind = 0xdead0004;
}

[shader("raygeneration")] void rayGenShaderIdentities()
{
    uint index = DispatchRaysIndex().x;
    uint primitive = index % 3;
    // These points are strictly inside the three triangles, away from shared edges.
    float x = primitive == 1 ? -0.25 : 0.25;
    float y = primitive == 2 ? -0.25 : 0.25;
    x += index >= 3 ? 4.0 : 0.0;
    if (index == 6)
        x = 10.0;

    RayDesc ray;
    ray.Origin = float3(x, y, 0.0);
    ray.Direction = float3(0.0, 0.0, 1.0);
    ray.TMin = 0.001;
    ray.TMax = 2.0;
    IdentityPayload payload = {};
    uint flags = index % 2 == 0 ? RAY_FLAG_FORCE_OPAQUE : RAY_FLAG_NONE;
    TraceRay(sceneBVH, flags, 0xff, 0, 0, 0, ray, payload);

    uint offset = 1 + index * 6;
    identityResults[offset] = payload.primitive;
    identityResults[offset + 1] = payload.instanceIndex;
    identityResults[offset + 2] = payload.instanceID;
    identityResults[offset + 3] = payload.hit;
    identityResults[offset + 4] = payload.rayFlags;
    identityResults[offset + 5] = payload.hitKind;
}

// This oracle has independent bindings so the existing identity and attribute tests stay unchanged.
uniform uint terminationMode;
uniform RWStructuredBuffer<uint> terminationResults;

[raypayload]
struct TerminationPayload
{
    uint value : read(caller, anyhit, closesthit, miss) : write(caller, anyhit);
    uint marker : read(caller, anyhit, closesthit, miss) : write(caller, anyhit);
    uint phase : read(caller) : write(caller, closesthit, miss);
};

[noinline]
void conditionalTermination()
{
    if (terminationMode == 2 || terminationMode == 5)
        AcceptHitAndEndSearch();
    if (terminationMode == 3 || terminationMode == 6)
        IgnoreHit();
}

[noinline]
void innerTermination(inout TerminationPayload payload)
{
    payload.value = 31;
    conditionalTermination();
    payload.value = 92;
}

[noinline]
void outerTermination(inout TerminationPayload payload)
{
    innerTermination(payload);
    payload.value = 93;
}

namespace OrdinaryTerminationNames
{
    [noinline]
    uint IgnoreHit(uint value)
    {
        return value + 1;
    }

    [noinline]
    uint AcceptHitAndEndSearch(uint value)
    {
        return value * 2;
    }
}

[shader("anyhit")]
void anyHitPayloadTermination(inout TerminationPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    payload.marker = 64 + terminationMode;
    if (terminationMode == 0)
    {
        payload.value = 11;
        AcceptHitAndEndSearch();
        payload.value = 91;
    }
    else if (terminationMode == 1)
    {
        payload.value = 12;
        IgnoreHit();
        payload.value = 92;
    }
    else if (terminationMode <= 4)
    {
        payload.value = 21;
        conditionalTermination();
        payload.value = 91;
    }
    else if (terminationMode <= 7)
    {
        outerTermination(payload);
    }
    else
    {
        payload.value = OrdinaryTerminationNames::AcceptHitAndEndSearch(OrdinaryTerminationNames::IgnoreHit(20));
    }
}

[shader("closesthit")]
void closestHitPayloadTermination(inout TerminationPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    payload.phase = 1;
}

[shader("miss")]
void missPayloadTermination(inout TerminationPayload payload)
{
    payload.phase = 2;
}

[shader("raygeneration")]
void rayGenPayloadTermination()
{
    TerminationPayload payload = {};
    payload.value = 7;
    RayDesc ray;
    ray.Origin = float3(0.25, 0.25, 0.0);
    ray.Direction = float3(0.0, 0.0, 1.0);
    ray.TMin = 0.001;
    ray.TMax = 2.0;
    TraceRay(sceneBVH, RAY_FLAG_NONE, 0xff, 0, 0, 0, ray, payload);
    terminationResults[1] = payload.value;
    terminationResults[2] = payload.marker;
    terminationResults[3] = payload.phase;
}
