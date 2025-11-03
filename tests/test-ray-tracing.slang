// ray-tracing-test-shaders.slang

[raypayload]
struct RayPayload
{
    float4 color : read(caller) : write(caller, closesthit, miss);
};

uniform uint2 dims;
uniform RWTexture2D resultTexture;
uniform RaytracingAccelerationStructure sceneBVH;

[shader("raygeneration")]
void rayGenShaderIdx0()
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
    TraceRay(sceneBVH, RAY_FLAG_NONE, ~0, 0, 0, 0, ray, payload);

    resultTexture[pixel] = payload.color;
}

[shader("raygeneration")]
void rayGenShaderIdx1()
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
    TraceRay(sceneBVH, RAY_FLAG_NONE, ~0, 1, 0, 1, ray, payload);

    resultTexture[pixel] = payload.color;
}

[shader("miss")]
void missShaderIdx0(inout RayPayload payload)
{
    payload.color = float4(1, 1, 1, 1);
}

[shader("miss")]
void missShaderIdx1(inout RayPayload payload)
{
    payload.color = float4(0, 0, 0, 1);
}

[shader("closesthit")]
void closestHitShaderIdx0(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    uint primitiveIndex = PrimitiveIndex();
    float4 color = float4(0, 0, 0, 1);
    color[primitiveIndex] = 1;
    payload.color = color;
}

[shader("closesthit")]
void closestHitShaderIdx1(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    uint primitiveIndex = PrimitiveIndex();
    float4 color = float4(1, 1, 1, 1);
    color[primitiveIndex] = 0;
    payload.color = color;
}
