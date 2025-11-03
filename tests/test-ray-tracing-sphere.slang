// test-ray-tracing-sphere.slang

// NOP shaders

[shader("miss")]
void missNOP(inout RayPayload payload)
{
    // Nop
}

[shader("closesthit")]
void closestHitNOP(inout RayPayload payload)
{
    // Nop
}

// Simple sphere intersection

[raypayload]
struct RayPayload
{
    float4 color : read(caller) : write(caller, closesthit, miss);
};

uniform uint2 dims;
uniform RWTexture2D resultTexture;
uniform RaytracingAccelerationStructure sceneBVH;

[shader("raygeneration")]
void rayGenShader()
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

// Sphere intrinsics called from closest hit

struct Result
{
    int isSphereHit;
    float4 spherePositionAndRadius;
};

[raypayload]
struct RayPayloadIntrinsics
{
    Result res : read(caller) : write(caller, closesthit);
};

uniform RWStructuredBuffer<Result> resultBuffer;

// Assume a single pixel launch. Trace a ray directly toward the target sphere and write out the
// results of calling the various sphere intrinsics.

[shader("raygeneration")]
void rayGenSphereIntrinsics()
{
    // Trace the ray.
    RayDesc ray;
    ray.Origin = {0.0f, 0.0f, 0.0f};
    ray.Direction = {0.0f, 0.0f, -1.0f};
    ray.TMin = 0.001;
    ray.TMax = 10000.0;
    RayPayloadIntrinsics payload = { {} };
    TraceRay(sceneBVH, RAY_FLAG_NONE, ~0, 0, 0, 0, ray, payload);

    resultBuffer[0] = payload.res;
}

[shader("closesthit")]
void closestHitSphereIntrinsics(inout RayPayloadIntrinsics payload, in BuiltInTriangleIntersectionAttributes attr)
{
    payload.res.isSphereHit = IsSphereHit();
    payload.res.spherePositionAndRadius = GetSpherePositionAndRadius();
}


// Hit object sphere intrinsics called from raygen

[shader("raygeneration")]
void rayGenSphereIntrinsicsHitObject()
{
    // Trace the ray.
    RayDesc ray;
    ray.Origin = {0.0f, 0.0f, 0.0f};
    ray.Direction = {0.0f, 0.0f, -1.0f};
    ray.TMin = 0.001;
    ray.TMax = 10000.0;
    RayPayload payload = { {} };
    HitObject hit = HitObject.TraceRay(sceneBVH, RAY_FLAG_NONE, ~0, 0, 0, 0, ray, payload);

    resultBuffer[0].isSphereHit = hit.IsSphereHit();
    resultBuffer[0].spherePositionAndRadius = hit.GetSpherePositionAndRadius();
}
