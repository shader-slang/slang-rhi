struct MatrixData
{
    float values[12];
};

struct Result
{
    MatrixData matrix;
};

uniform RaytracingAccelerationStructure sceneBVH;
uniform RWStructuredBuffer<Result> resultBuffer;

[raypayload]
struct RayPayload
{
    MatrixData matrix : read(caller) : write(caller, closesthit);;
};

MatrixData flattenFloat3x4(float3x4 value)
{
    MatrixData result;
    [unroll]
    for (uint row = 0; row < 3; ++row)
    {
        [unroll]
        for (uint column = 0; column < 4; ++column)
        {
            result.values[row * 4 + column] = value[row][column];
        }
    }
    return result;
}

MatrixData flattenFloat4x3(float4x3 value)
{
    MatrixData result;
    [unroll]
    for (uint row = 0; row < 4; ++row)
    {
        [unroll]
        for (uint column = 0; column < 3; ++column)
        {
            result.values[row * 3 + column] = value[row][column];
        }
    }
    return result;
}

// Triangle was transformed by x=1, y=2, z=3 (see kInstanceTransform in test-ray-tracing-transform-query.cpp)
static const float3 kTrianglePos = float3(0.25f, 0.25f, 1.0f);
static const float3 kObjectToWorld3x4Translation = float3(1.0f, 2.0f, 3.0f);
static const float3 kTranslatedTrianglePos = kTrianglePos + kObjectToWorld3x4Translation;

[shader("raygeneration")]
void rayGenShaderObjectToWorld3x4()
{
    float3 origin = {0.0, 0.0, 0.0};
    float3 dir = kTranslatedTrianglePos - origin;

    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = dir;
    ray.TMin = 0.001;
    ray.TMax = 10000.0;

    RayPayload payload = {};
    TraceRay(sceneBVH, RAY_FLAG_NONE, ~0, 0, 0, 0, ray, payload);
    resultBuffer[0].matrix = payload.matrix;
}

[shader("raygeneration")]
void rayGenShaderWorldToObject3x4()
{
    float3 origin = {0.0, 0.0, 0.0};
    float3 dir = kTranslatedTrianglePos - origin;

    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = dir;
    ray.TMin = 0.001;
    ray.TMax = 10000.0;

    RayPayload payload = {};
    TraceRay(sceneBVH, RAY_FLAG_NONE, ~0, 0, 0, 0, ray, payload);
    resultBuffer[0].matrix = payload.matrix;
}

[shader("raygeneration")]
void rayGenShaderObjectToWorld4x3()
{
    float3 origin = {0.0, 0.0, 0.0};
    float3 dir = kTranslatedTrianglePos - origin;

    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = dir;
    ray.TMin = 0.001;
    ray.TMax = 10000.0;

    RayPayload payload = {};
    TraceRay(sceneBVH, RAY_FLAG_NONE, ~0, 0, 0, 0, ray, payload);
    resultBuffer[0].matrix = payload.matrix;
}

[shader("raygeneration")]
void rayGenShaderWorldToObject4x3()
{
    float3 origin = {0.0, 0.0, 0.0};
    float3 dir = kTranslatedTrianglePos - origin;

    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = dir;
    ray.TMin = 0.001;
    ray.TMax = 10000.0;

    RayPayload payload = {};
    TraceRay(sceneBVH, RAY_FLAG_NONE, ~0, 0, 0, 0, ray, payload);
    resultBuffer[0].matrix = payload.matrix;
}

[shader("raygeneration")]
void rayGenShaderHitObjectGetWorldToObject()
{
    float3 origin = {0.0, 0.0, 0.0};
    float3 dir = kTranslatedTrianglePos - origin;

    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = dir;
    ray.TMin = 0.001;
    ray.TMax = 10000.0;

    RayPayload payload = {};
    HitObject hit = HitObject.TraceRay(sceneBVH, RAY_FLAG_NONE, ~0, 0, 0, 0, ray, payload);

    MatrixData matrix = flattenFloat4x3(hit.GetWorldToObject());
    resultBuffer[0].matrix = matrix;
}

[shader("raygeneration")]
void rayGenShaderHitObjectGetObjectToWorld()
{
    float3 origin = {0.0, 0.0, 0.0};
    float3 dir = kTranslatedTrianglePos - origin;

    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = dir;
    ray.TMin = 0.001;
    ray.TMax = 10000.0;

    RayPayload payload = {};
    HitObject hit = HitObject.TraceRay(sceneBVH, RAY_FLAG_NONE, ~0, 0, 0, 0, ray, payload);

    MatrixData matrix = flattenFloat4x3(hit.GetObjectToWorld());
    resultBuffer[0].matrix = matrix;
}

[shader("closesthit")]
void closestHitObjectToWorld3x4(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    payload.matrix = flattenFloat3x4(ObjectToWorld3x4());
}

[shader("closesthit")]
void closestHitWorldToObject3x4(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    payload.matrix = flattenFloat3x4(WorldToObject3x4());
}

[shader("closesthit")]
void closestHitObjectToWorld4x3(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    payload.matrix = flattenFloat4x3(ObjectToWorld4x3());
}

[shader("closesthit")]
void closestHitWorldToObject4x3(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    payload.matrix = flattenFloat4x3(WorldToObject4x3());
}

[shader("closesthit")]
void closestHitNOP(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    // Payload is unused for the HitObject test.
}

[shader("miss")]
void missNOP(inout RayPayload payload)
{
    // No-op miss shader.
}
