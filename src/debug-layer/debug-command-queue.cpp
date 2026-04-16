#include "debug-command-queue.h"
#include "debug-command-buffer.h"
#include "debug-fence.h"
#include "debug-helper-functions.h"

#include <vector>

namespace rhi::debug {

QueueType DebugCommandQueue::getType()
{
    SLANG_RHI_DEBUG_API(ICommandQueue, getType);

    return baseObject->getType();
}

Result DebugCommandQueue::createCommandEncoder(const CommandEncoderDesc& desc, ICommandEncoder** outEncoder)
{
    SLANG_RHI_DEBUG_API(ICommandQueue, createCommandEncoder);

    if (!outEncoder)
    {
        RHI_VALIDATION_ERROR("'outEncoder' must not be null.");
        return SLANG_E_INVALID_ARG;
    }

    RefPtr<DebugCommandEncoder> encoder = new DebugCommandEncoder(ctx);
    SLANG_RETURN_ON_FAIL(baseObject->createCommandEncoder(desc, encoder->baseObject.writeRef()));

    returnComPtr(outEncoder, encoder);
    return SLANG_OK;
}

Result DebugCommandQueue::submit(const SubmitDesc& desc)
{
    SLANG_RHI_DEBUG_API(ICommandQueue, submit);

    if (desc.commandBufferCount > 0 && !desc.commandBuffers)
    {
        RHI_VALIDATION_ERROR("'desc.commandBuffers' must not be null when 'commandBufferCount' > 0.");
        return SLANG_E_INVALID_ARG;
    }
    if (desc.waitFenceCount > 0 && !desc.waitFences)
    {
        RHI_VALIDATION_ERROR("'desc.waitFences' must not be null when 'waitFenceCount' > 0.");
        return SLANG_E_INVALID_ARG;
    }
    if (desc.waitFenceCount > 0 && !desc.waitFenceValues)
    {
        RHI_VALIDATION_ERROR("'desc.waitFenceValues' must not be null when 'waitFenceCount' > 0.");
        return SLANG_E_INVALID_ARG;
    }
    if (desc.signalFenceCount > 0 && !desc.signalFences)
    {
        RHI_VALIDATION_ERROR("'desc.signalFences' must not be null when 'signalFenceCount' > 0.");
        return SLANG_E_INVALID_ARG;
    }
    if (desc.signalFenceCount > 0 && !desc.signalFenceValues)
    {
        RHI_VALIDATION_ERROR("'desc.signalFenceValues' must not be null when 'signalFenceCount' > 0.");
        return SLANG_E_INVALID_ARG;
    }

    short_vector<ICommandBuffer*> innerCommandBuffers;
    short_vector<IFence*> innerWaitFences;
    short_vector<IFence*> innerSignalFences;
    for (uint32_t i = 0; i < desc.commandBufferCount; ++i)
    {
        if (!desc.commandBuffers[i])
        {
            RHI_VALIDATION_ERROR_FORMAT("'desc.commandBuffers[%u]' must not be null.", i);
            return SLANG_E_INVALID_ARG;
        }
        innerCommandBuffers.push_back(getInnerObj(desc.commandBuffers[i]));
    }
    for (uint32_t i = 0; i < desc.waitFenceCount; ++i)
    {
        if (!desc.waitFences[i])
        {
            RHI_VALIDATION_ERROR_FORMAT("'desc.waitFences[%u]' must not be null.", i);
            return SLANG_E_INVALID_ARG;
        }
        innerWaitFences.push_back(getInnerObj(desc.waitFences[i]));
    }
    for (uint32_t i = 0; i < desc.signalFenceCount; ++i)
    {
        if (!desc.signalFences[i])
        {
            RHI_VALIDATION_ERROR_FORMAT("'desc.signalFences[%u]' must not be null.", i);
            return SLANG_E_INVALID_ARG;
        }
        innerSignalFences.push_back(getInnerObj(desc.signalFences[i]));
        getDebugObj(desc.signalFences[i])->maxValueToSignal =
            max(getDebugObj(desc.signalFences[i])->maxValueToSignal, desc.signalFenceValues[i]);
    }

    SubmitDesc innerDesc = desc;
    innerDesc.commandBuffers = innerCommandBuffers.data();
    innerDesc.waitFences = innerWaitFences.data();
    innerDesc.signalFences = innerSignalFences.data();

    return baseObject->submit(innerDesc);
}

Result DebugCommandQueue::waitOnHost()
{
    SLANG_RHI_DEBUG_API(ICommandQueue, waitOnHost);

    return baseObject->waitOnHost();
}

Result DebugCommandQueue::getNativeHandle(NativeHandle* outHandle)
{
    SLANG_RHI_DEBUG_API(ICommandQueue, getNativeHandle);

    if (!outHandle)
    {
        RHI_VALIDATION_ERROR("'outHandle' must not be null.");
        return SLANG_E_INVALID_ARG;
    }

    return baseObject->getNativeHandle(outHandle);
}

} // namespace rhi::debug
