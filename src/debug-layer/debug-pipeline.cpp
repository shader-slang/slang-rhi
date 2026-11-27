#include "debug-pipeline.h"
#include "debug-helper-functions.h"

namespace rhi::debug {

// ----------------------------------------------------------------------------
// DebugRenderPipeline
// ----------------------------------------------------------------------------

IRenderPipeline* DebugRenderPipeline::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IPipeline::getTypeGuid() ||
        guid == IRenderPipeline::getTypeGuid())
        return static_cast<IRenderPipeline*>(this);
    return nullptr;
}

Result DebugRenderPipeline::getNativeHandle(NativeHandle* outHandle)
{
    SLANG_RHI_DEBUG_API(IRenderPipeline, getNativeHandle);

    if (!outHandle)
    {
        RHI_VALIDATION_ERROR("'outHandle' must not be null.");
        return SLANG_E_INVALID_ARG;
    }

    return baseObject->getNativeHandle(outHandle);
}

// ----------------------------------------------------------------------------
// DebugComputePipeline
// ----------------------------------------------------------------------------

IComputePipeline* DebugComputePipeline::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IPipeline::getTypeGuid() ||
        guid == IComputePipeline::getTypeGuid())
        return static_cast<IComputePipeline*>(this);
    return nullptr;
}

IShaderProgram* DebugComputePipeline::getProgram()
{
    SLANG_RHI_DEBUG_API(IComputePipeline, getProgram);

    return baseObject->getProgram();
}

const ComputePipelineDesc& DebugComputePipeline::getDesc()
{
    SLANG_RHI_DEBUG_API(IComputePipeline, getDesc);

    return baseObject->getDesc();
}

Result DebugComputePipeline::getNativeHandle(NativeHandle* outHandle)
{
    SLANG_RHI_DEBUG_API(IComputePipeline, getNativeHandle);

    if (!outHandle)
    {
        RHI_VALIDATION_ERROR("'outHandle' must not be null.");
        return SLANG_E_INVALID_ARG;
    }

    return baseObject->getNativeHandle(outHandle);
}

Result DebugComputePipeline::getMaxCooperativeThreadGroupCount(IShaderObject* rootObject, uint32_t* outCount)
{
    SLANG_RHI_DEBUG_API(IComputePipeline, getMaxCooperativeThreadGroupCount);

    if (!outCount)
    {
        RHI_VALIDATION_ERROR("'outCount' must not be null.");
        return SLANG_E_INVALID_ARG;
    }

    return baseObject->getMaxCooperativeThreadGroupCount(getInnerObj(rootObject), outCount);
}

// ----------------------------------------------------------------------------
// DebugRayTracingPipeline
// ----------------------------------------------------------------------------

IRayTracingPipeline* DebugRayTracingPipeline::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IPipeline::getTypeGuid() ||
        guid == IRayTracingPipeline::getTypeGuid())
        return static_cast<IRayTracingPipeline*>(this);
    return nullptr;
}

Result DebugRayTracingPipeline::getNativeHandle(NativeHandle* outHandle)
{
    SLANG_RHI_DEBUG_API(IRayTracingPipeline, getNativeHandle);

    if (!outHandle)
    {
        RHI_VALIDATION_ERROR("'outHandle' must not be null.");
        return SLANG_E_INVALID_ARG;
    }

    return baseObject->getNativeHandle(outHandle);
}

} // namespace rhi::debug
