// Instance-scoped matrices only; the public active-transform-list matrix APIs remain separate.
uniform RaytracingAccelerationStructure sceneBVH;
uniform RWStructuredBuffer<uint> instanceMatrixResults;

[raypayload]
struct InstanceMatrixPayload
{
    float coefficients[24] : read(caller) : write(caller, closesthit);
    uint phase : read(caller) : write(caller, closesthit, miss);
};

[shader("closesthit")]
void instanceMatrixClosestHit(inout InstanceMatrixPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    uint64_t handle = GetTransformListHandle(0);
    float3x4 forward = GetTraversableObjectToWorld(handle);
    float3x4 inverse = GetTraversableWorldToObject(handle);
    [unroll]
    for (uint row = 0; row < 3; ++row)
    {
        [unroll]
        for (uint column = 0; column < 4; ++column)
        {
            payload.coefficients[row * 4 + column] = forward[row][column];
            payload.coefficients[12 + row * 4 + column] = inverse[row][column];
        }
    }
    payload.phase = 1;
}

[shader("miss")]
void instanceMatrixMiss(inout InstanceMatrixPayload payload)
{
    payload.phase = 2;
}

[shader("raygeneration")]
void instanceMatrixRayGen()
{
    uint rayIndex = DispatchRaysIndex().x;
    RayDesc ray;
    // These are the host-authored affine images of object origin (.25,.25,0) and direction (0,0,1).
    // Each ray hits object point (.25,.25,1) at t=1; neither ray intersects the other instance.
    if (rayIndex == 0)
    {
        ray.Origin = float3(0.5, 3.25, 3.0);
        ray.Direction = float3(0.0, 0.0, 8.0);
    }
    else if (rayIndex == 1)
    {
        ray.Origin = float3(8.5, -2.0, 3.0);
        ray.Direction = float3(1.0, -4.0, 2.0);
    }
    else
    {
        ray.Origin = float3(40.0, 40.0, 0.0);
        ray.Direction = float3(0.0, 0.0, 1.0);
    }
    ray.TMin = 0.25;
    ray.TMax = 1.5;
    InstanceMatrixPayload payload = {};
    [unroll]
    for (uint word = 0; word < 24; ++word)
        payload.coefficients[word] = -float(128 + word);
    TraceRay(sceneBVH, RAY_FLAG_NONE, 0xff, 0, 0, 0, ray, payload);
    uint offset = 1 + rayIndex * 25;
    [unroll]
    for (uint word = 0; word < 24; ++word)
        instanceMatrixResults[offset + word] = asuint(payload.coefficients[word]);
    instanceMatrixResults[offset + 24] = payload.phase;
}
