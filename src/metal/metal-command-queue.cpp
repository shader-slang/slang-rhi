#include "metal-command-queue.h"
#include "metal-command-buffer.h"
#include "metal-fence.h"
#include "metal-util.h"

namespace rhi::metal {

CommandQueueImpl::CommandQueueImpl(DeviceImpl* device, QueueType type)
    : CommandQueue(device, type)
{
}

CommandQueueImpl::~CommandQueueImpl() {}

void CommandQueueImpl::init(NS::SharedPtr<MTL::CommandQueue> commandQueue)
{
    m_commandQueue = commandQueue;
}

void CommandQueueImpl::waitOnHost()
{
    // TODO implement
}

Result CommandQueueImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::MTLCommandQueue;
    outHandle->value = (uint64_t)m_commandQueue.get();
    return SLANG_OK;
}

Result CommandQueueImpl::waitForFenceValuesOnDevice(GfxCount fenceCount, IFence** fences, uint64_t* waitValues)
{
    for (GfxCount i = 0; i < fenceCount; ++i)
    {
        FenceWaitInfo waitInfo;
        waitInfo.fence = checked_cast<FenceImpl*>(fences[i]);
        waitInfo.waitValue = waitValues[i];
        m_pendingWaitFences.push_back(waitInfo);
    }
    return SLANG_OK;
}

void CommandQueueImpl::queueSubmitImpl(
    uint32_t count,
    ICommandBuffer* const* commandBuffers,
    IFence* fence,
    uint64_t valueToSignal
)
{
    // If there are any pending wait fences, encode them to a new command buffer.
    // Metal ensures that command buffers are executed in the order they are committed.
    if (m_pendingWaitFences.size() > 0)
    {
        MTL::CommandBuffer* commandBuffer = m_commandQueue->commandBuffer();
        for (const auto& fenceInfo : m_pendingWaitFences)
        {
            commandBuffer->encodeWait(fenceInfo.fence->m_event.get(), fenceInfo.waitValue);
        }
        commandBuffer->commit();
        m_pendingWaitFences.clear();
    }

    for (uint32_t i = 0; i < count; ++i)
    {
        CommandBufferImpl* cmdBufImpl = checked_cast<CommandBufferImpl*>(commandBuffers[i]);
        // If this is the last command buffer and a fence is provided, signal the fence.
        if (i == count - 1 && fence != nullptr)
        {
            cmdBufImpl->m_commandBuffer->encodeSignalEvent(
                checked_cast<FenceImpl*>(fence)->m_event.get(),
                valueToSignal
            );
        }
        cmdBufImpl->m_commandBuffer->commit();
    }

    // If there are no command buffers to submit, but a fence is provided, signal the fence.
    if (count == 0 && fence != nullptr)
    {
        MTL::CommandBuffer* commandBuffer = m_commandQueue->commandBuffer();
        commandBuffer->encodeSignalEvent(checked_cast<FenceImpl*>(fence)->m_event.get(), valueToSignal);
        commandBuffer->commit();
    }
}

void CommandQueueImpl::submit(
    GfxCount count,
    ICommandBuffer* const* commandBuffers,
    IFence* fence,
    uint64_t valueToSignal
)
{
    AUTORELEASEPOOL

    if (count == 0 && fence == nullptr)
    {
        return;
    }
    queueSubmitImpl(count, commandBuffers, fence, valueToSignal);
}

} // namespace rhi::metal
