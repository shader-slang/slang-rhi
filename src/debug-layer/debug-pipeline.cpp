#include "debug-pipeline.h"
#include "debug-helper-functions.h"

namespace rhi::debug {

// ----------------------------------------------------------------------------
// DebugRenderPipeline
// ----------------------------------------------------------------------------

Result DebugRenderPipeline::getNativeHandle(NativeHandle* outHandle)
{
    SLANG_RHI_DEBUG_API(IRenderPipeline, getNativeHandle);

    return baseObject->getNativeHandle(outHandle);
}

// ----------------------------------------------------------------------------
// DebugComputePipeline
// ----------------------------------------------------------------------------

Result DebugComputePipeline::getNativeHandle(NativeHandle* outHandle)
{
    SLANG_RHI_DEBUG_API(IComputePipeline, getNativeHandle);

    return baseObject->getNativeHandle(outHandle);
}

// ----------------------------------------------------------------------------
// DebugRayTracingPipeline
// ----------------------------------------------------------------------------

Result DebugRayTracingPipeline::getNativeHandle(NativeHandle* outHandle)
{
    SLANG_RHI_DEBUG_API(IRayTracingPipeline, getNativeHandle);

    return baseObject->getNativeHandle(outHandle);
}

} // namespace rhi::debug
