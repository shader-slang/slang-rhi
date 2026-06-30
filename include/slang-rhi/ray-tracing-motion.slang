#pragma once

namespace rhi
{

/// Traces a ray at a specific motion time.
///
/// Slang's standard TraceMotionRay is currently restricted to SPIR-V targets. The CUDA path
/// below maps to the time-aware OptiX trace helper in Slang's CUDA prelude.
void traceMotionRay<T>(
    RaytracingAccelerationStructure accelerationStructure,
    uint rayFlags,
    uint instanceInclusionMask,
    uint rayContributionToHitGroupIndex,
    uint multiplierForGeometryContributionToHitGroupIndex,
    uint missShaderIndex,
    RayDesc ray,
    float currentTime,
    inout T payload
)
{
#if defined(__CUDA__)
    __requirePrelude(R"(
template<typename T>
__forceinline__ __device__ void slangRhiOptixTraceMotion(
    OptixTraversableHandle accelerationStructure,
    uint32_t rayFlags,
    uint32_t instanceInclusionMask,
    uint32_t rayContributionToHitGroupIndex,
    uint32_t multiplierForGeometryContributionToHitGroupIndex,
    uint32_t missShaderIndex,
    RayDesc ray,
    float rayTime,
    T* payload)
{
    constexpr size_t numRegs = (sizeof(T) + 3) / 4;
    if constexpr (numRegs <= kMaxOptiXPayloadRegisters)
    {
        PayloadRegisters<T> registers;
        registers.pack(*payload);
        optixTraceWithRegs<T>(
            accelerationStructure,
            ray.Origin,
            ray.Direction,
            ray.TMin,
            ray.TMax,
            rayTime,
            instanceInclusionMask,
            rayFlags,
            rayContributionToHitGroupIndex,
            multiplierForGeometryContributionToHitGroupIndex,
            missShaderIndex,
            registers);
        registers.unpack(*payload);
    }
    else
    {
        uint32_t r0, r1;
        packOptiXRayPayloadPointer((void*)payload, r0, r1);
        optixTrace(
            accelerationStructure,
            ray.Origin,
            ray.Direction,
            ray.TMin,
            ray.TMax,
            rayTime,
            instanceInclusionMask,
            rayFlags,
            rayContributionToHitGroupIndex,
            multiplierForGeometryContributionToHitGroupIndex,
            missShaderIndex,
            r0,
            r1);
    }
}
)");
    __intrinsic_asm
        "slangRhiOptixTraceMotion($0, $1, $2, $3, $4, $5, $6, $7, $8)";
#else
    TraceMotionRay(
        accelerationStructure,
        rayFlags,
        instanceInclusionMask,
        rayContributionToHitGroupIndex,
        multiplierForGeometryContributionToHitGroupIndex,
        missShaderIndex,
        ray,
        currentTime,
        payload
    );
#endif
}

/// Returns the motion time of the current ray.
float rayCurrentTime()
{
#if defined(__CUDA__)
    __intrinsic_asm "optixGetRayTime()";
#else
    return RayCurrentTime();
#endif
}

} // namespace rhi
