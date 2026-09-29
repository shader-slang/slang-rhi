// test-ray-tracing-lss-inline-rq.slang

// Inline ray-query LSS intrinsics test, kept in its own module.
//
// The inline RayQuery<>.CommittedIsLssNV() / CommittedLssObjectPositionsAndRadiiNV()
// accessors require the rayquery_lss_nv capability (glsl/hlsl/spirv); they have no
// CUDA target support in public slang, so rayGenLssInlineRayQuery cannot be compiled
// for CUDA. It lives here rather than in test-ray-tracing-lss.slang so that the LSS
// tests which DO run on CUDA (intersection / hit-object / closesthit intrinsics) are
// not poisoned by an E36107 "unavailable features for cuda" error on an entry point
// they never use. On CUDA the inline-ray-query test skips (no RayQuery feature), so
// this module is never compiled there; on Vulkan/D3D12 with LSS support it runs.

[raypayload]
struct RayPayload
{
    float4 color : read(caller) : write(caller, closesthit, miss);
};

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

struct Result
{
    int isLssHit;
    float2x4 lssPositionsAndRadii;
};

uniform RaytracingAccelerationStructure sceneBVH;
uniform RWStructuredBuffer<Result> resultBuffer;

// Assume a single pixel launch. Trace a ray straight down the -z axis into the
// single-segment LSS, let the fixed-function traversal commit the built-in curve
// hit, then read back the committed LSS classification and endpoint
// positions/radii via the inline ray-query accessors.
[shader("raygeneration")]
void rayGenLssInlineRayQuery()
{
    RayDesc ray;
    ray.Origin = {0.0f, 0.0f, 0.0f};
    ray.Direction = {0.0f, 0.0f, -1.0f};
    ray.TMin = 0.001;
    ray.TMax = 10000.0;

    RayQuery<RAY_FLAG_NONE> q;
    q.TraceRayInline(sceneBVH, RAY_FLAG_NONE, ~0, ray);
    while (q.Proceed())
    {
    }

    Result res = {};
    res.isLssHit = q.CommittedIsLssNV() ? 1 : 0;
    res.lssPositionsAndRadii = q.CommittedLssObjectPositionsAndRadiiNV();
    resultBuffer[0] = res;
}
