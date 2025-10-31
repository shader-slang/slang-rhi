// ray-tracing-test-reorder.slang

[raypayload]
struct RayPayload
{
    float4 color : read(caller) : write(caller, closesthit, miss);
};

uniform uint2 dims;
uniform RWTexture2D resultTexture;
uniform RaytracingAccelerationStructure sceneBVH;

[shader("raygeneration")]
void rayGenShaderReorderHint()
{
    uint2 pixel = DispatchRaysIndex().xy;
    if (any(pixel >= dims))
        return;

    float2 uv = float2(pixel) / float2(dims - 1);

    // Trace the ray.
    RayDesc ray;
    ray.Origin = float3(uv * 2.0 - 1.0, 0.0);
    ray.Direction = float3(0, 0, 1);
    ray.TMin = 0.001;
    ray.TMax = 10000.0;
    RayPayload payload = { float4(0, 0, 0, 0) };
    HitObject hit = HitObject.TraceRay(sceneBVH, RAY_FLAG_NONE, ~0, 0, 0, 0, ray, payload);

    uint hint = 1;
    uint numHintBits = 1;
    ReorderThread(hint, numHintBits);

    HitObject.Invoke(sceneBVH, hit, payload);
    resultTexture[pixel] = payload.color;
}

[shader("raygeneration")]
void rayGenShaderReorderHitObj()
{
    uint2 pixel = DispatchRaysIndex().xy;
    if (any(pixel >= dims))
        return;

    float2 uv = float2(pixel) / float2(dims - 1);

    // Trace the ray.
    RayDesc ray;
    ray.Origin = float3(uv * 2.0 - 1.0, 0.0);
    ray.Direction = float3(0, 0, 1);
    ray.TMin = 0.001;
    ray.TMax = 10000.0;
    RayPayload payload = { float4(0, 0, 0, 0) };
    HitObject hit = HitObject.TraceRay(sceneBVH, RAY_FLAG_NONE, ~0, 0, 0, 0, ray, payload);

    ReorderThread(hit);

    HitObject.Invoke(sceneBVH, hit, payload);
    resultTexture[pixel] = payload.color;
}

[shader("raygeneration")]
void rayGenShaderReorderHitObjAndHint()
{
    uint2 pixel = DispatchRaysIndex().xy;
    if (any(pixel >= dims))
        return;

    float2 uv = float2(pixel) / float2(dims - 1);

    // Trace the ray.
    RayDesc ray;
    ray.Origin = float3(uv * 2.0 - 1.0, 0.0);
    ray.Direction = float3(0, 0, 1);
    ray.TMin = 0.001;
    ray.TMax = 10000.0;
    RayPayload payload = { float4(0, 0, 0, 0) };
    HitObject hit = HitObject.TraceRay(sceneBVH, RAY_FLAG_NONE, ~0, 0, 0, 0, ray, payload);

    uint hint = 1;
    uint numHintBits = 1;
    ReorderThread(hit, hint, numHintBits);

    HitObject.Invoke(sceneBVH, hit, payload);
    resultTexture[pixel] = payload.color;
}

[shader("miss")]
void missShader(inout RayPayload payload)
{
    payload.color = float4(1, 1, 1, 1);
}

[shader("closesthit")]
void closestHitShader(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    uint primitiveIndex = PrimitiveIndex();
    float4 color = float4(0, 0, 0, 1);
    color[primitiveIndex] = 1;
    payload.color = color;
}
