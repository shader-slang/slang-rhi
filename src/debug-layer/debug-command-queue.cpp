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

Result DebugCommandQueue::submit(
    GfxCount count,
    ICommandBuffer* const* commandBuffers,
    IFence* fence,
    uint64_t valueToSignal
)
{
    SLANG_RHI_API_FUNC;
    std::vector<ICommandBuffer*> innerCommandBuffers;
    for (GfxIndex i = 0; i < count; i++)
    {
        auto cmdBufferIn = commandBuffers[i];
        auto cmdBufferImpl = getDebugObj(cmdBufferIn);
        auto innerCmdBuffer = getInnerObj(cmdBufferIn);
        innerCommandBuffers.push_back(innerCmdBuffer);
    }
    Result result = baseObject->submit(count, innerCommandBuffers.data(), getInnerObj(fence), valueToSignal);
    if (fence)
    {
        getDebugObj(fence)->maxValueToSignal = max(getDebugObj(fence)->maxValueToSignal, valueToSignal);
    }
    return result;
}

void DebugCommandQueue::waitOnHost()
{
    SLANG_RHI_API_FUNC;
    baseObject->waitOnHost();
}

Result DebugCommandQueue::waitForFenceValuesOnDevice(GfxCount fenceCount, IFence** fences, uint64_t* waitValues)
{
    SLANG_RHI_API_FUNC;
    std::vector<IFence*> innerFences;
    for (GfxIndex i = 0; i < fenceCount; ++i)
    {
        innerFences.push_back(getInnerObj(fences[i]));
    }
    return baseObject->waitForFenceValuesOnDevice(fenceCount, innerFences.data(), waitValues);
}

Result DebugCommandQueue::getNativeHandle(NativeHandle* outHandle)
{
    SLANG_RHI_API_FUNC;
    return baseObject->getNativeHandle(outHandle);
}

} // namespace rhi::debug
