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

    RefPtr<DebugCommandEncoder> encoder = new DebugCommandEncoder(ctx);
    SLANG_RETURN_ON_FAIL(baseObject->createCommandEncoder(desc, encoder->baseObject.writeRef()));

    returnComPtr(outEncoder, encoder);
    return SLANG_OK;
}

Result DebugCommandQueue::submit(const SubmitDesc& desc)
{
    SLANG_RHI_DEBUG_API(ICommandQueue, submit);

    short_vector<ICommandBuffer*> innerCommandBuffers;
    short_vector<IFence*> innerWaitFences;
    short_vector<IFence*> innerSignalFences;
    for (uint32_t i = 0; i < desc.commandBufferCount; ++i)
    {
        if (!desc.commandBuffers[i])
        {
            RHI_VALIDATION_ERROR("Command buffer is null.");
            return SLANG_E_INVALID_ARG;
        }
        innerCommandBuffers.push_back(getInnerObj(desc.commandBuffers[i]));
    }
    for (uint32_t i = 0; i < desc.waitFenceCount; ++i)
    {
        innerWaitFences.push_back(getInnerObj(desc.waitFences[i]));
    }
    for (uint32_t i = 0; i < desc.signalFenceCount; ++i)
    {
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

    return baseObject->getNativeHandle(outHandle);
}

} // namespace rhi::debug
