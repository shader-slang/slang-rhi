#include "debug-command-queue.h"
#include "debug-command-buffer.h"
#include "debug-fence.h"
#include "debug-helper-functions.h"

#include <vector>

namespace rhi::debug {

QueueType DebugCommandQueue::getType()
{
    SLANG_RHI_API_FUNC;
    return baseObject->getType();
}

Result DebugCommandQueue::createCommandEncoder(ICommandEncoder** outEncoder)
{
    SLANG_RHI_API_FUNC;
    RefPtr<DebugCommandEncoder> encoder = new DebugCommandEncoder(ctx);
    auto result = baseObject->createCommandEncoder(encoder->baseObject.writeRef());
    if (SLANG_FAILED(result))
        return result;
    returnComPtr(outEncoder, encoder);
    return result;
}

Result DebugCommandQueue::submit(const SubmitDesc& desc)
{
    SLANG_RHI_API_FUNC;
    short_vector<ICommandBuffer*> innerCommandBuffers;
    short_vector<IFence*> innerWaitFences;
    short_vector<IFence*> innerSignalFences;
    for (uint32_t i = 0; i < desc.commandBufferCount; ++i)
    {
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
    SLANG_RHI_API_FUNC;
    return baseObject->waitOnHost();
}

Result DebugCommandQueue::getNativeHandle(NativeHandle* outHandle)
{
    SLANG_RHI_API_FUNC;
    return baseObject->getNativeHandle(outHandle);
}

} // namespace rhi::debug
