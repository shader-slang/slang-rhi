#include "vk-command-queue.h"
#include "vk-command-buffer.h"
#include "vk-fence.h"
#include "vk-transient-heap.h"

#include "core/static_vector.h"

namespace rhi::vk {

ICommandQueue* CommandQueueImpl::getInterface(const Guid& guid)
{
    if (guid == GUID::IID_ISlangUnknown || guid == GUID::IID_ICommandQueue)
        return static_cast<ICommandQueue*>(this);
    return nullptr;
}

CommandQueueImpl::~CommandQueueImpl()
{
    m_renderer->m_api.vkQueueWaitIdle(m_queue);

    m_renderer->m_queueAllocCount--;
    m_renderer->m_api.vkDestroySemaphore(m_renderer->m_api.m_device, m_semaphore, nullptr);
}

void CommandQueueImpl::init(DeviceImpl* renderer, VkQueue queue, uint32_t queueFamilyIndex)
{
    m_renderer = renderer;
    m_queue = queue;
    m_queueFamilyIndex = queueFamilyIndex;
    VkSemaphoreCreateInfo semaphoreCreateInfo = {};
    semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    semaphoreCreateInfo.flags = 0;
    m_renderer->m_api.vkCreateSemaphore(m_renderer->m_api.m_device, &semaphoreCreateInfo, nullptr, &m_semaphore);
}

void CommandQueueImpl::waitOnHost()
{
    auto& vkAPI = m_renderer->m_api;
    vkAPI.vkQueueWaitIdle(m_queue);
}

Result CommandQueueImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::VkQueue;
    outHandle->value = (uint64_t)m_queue;
    return SLANG_OK;
}

const CommandQueueImpl::Desc& CommandQueueImpl::getDesc()
{
    return m_desc;
}

Result CommandQueueImpl::waitForFenceValuesOnDevice(GfxCount fenceCount, IFence** fences, uint64_t* waitValues)
{
    for (GfxIndex i = 0; i < fenceCount; ++i)
    {
        FenceWaitInfo waitInfo;
        waitInfo.fence = static_cast<FenceImpl*>(fences[i]);
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
    auto& vkAPI = m_renderer->m_api;
    m_submitCommandBuffers.clear();
    for (uint32_t i = 0; i < count; i++)
    {
        auto cmdBufImpl = static_cast<CommandBufferImpl*>(commandBuffers[i]);
        if (!cmdBufImpl->m_isPreCommandBufferEmpty)
            m_submitCommandBuffers.push_back(cmdBufImpl->m_preCommandBuffer);
        auto vkCmdBuf = cmdBufImpl->m_commandBuffer;
        m_submitCommandBuffers.push_back(vkCmdBuf);
    }
    static_vector<VkSemaphore, 2> signalSemaphores;
    static_vector<uint64_t, 2> signalValues;
    signalSemaphores.push_back(m_semaphore);
    signalValues.push_back(0);

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    VkPipelineStageFlags stageFlag[] = {VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT};
    submitInfo.pWaitDstStageMask = stageFlag;
    submitInfo.commandBufferCount = (uint32_t)m_submitCommandBuffers.size();
    submitInfo.pCommandBuffers = m_submitCommandBuffers.data();
    static_vector<VkSemaphore, 3> waitSemaphores;
    static_vector<uint64_t, 3> waitValues;
    for (auto s : m_pendingWaitSemaphores)
    {
        if (s != VK_NULL_HANDLE)
        {
            waitSemaphores.push_back(s);
            waitValues.push_back(0);
        }
    }
    for (auto& fenceWait : m_pendingWaitFences)
    {
        waitSemaphores.push_back(fenceWait.fence->m_semaphore);
        waitValues.push_back(fenceWait.waitValue);
    }
    m_pendingWaitFences.clear();
    VkTimelineSemaphoreSubmitInfo timelineSubmitInfo = {VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO};
    if (fence)
    {
        auto fenceImpl = static_cast<FenceImpl*>(fence);
        signalSemaphores.push_back(fenceImpl->m_semaphore);
        signalValues.push_back(valueToSignal);
        submitInfo.pNext = &timelineSubmitInfo;
        timelineSubmitInfo.signalSemaphoreValueCount = (uint32_t)signalValues.size();
        timelineSubmitInfo.pSignalSemaphoreValues = signalValues.data();
        timelineSubmitInfo.waitSemaphoreValueCount = (uint32_t)waitValues.size();
        timelineSubmitInfo.pWaitSemaphoreValues = waitValues.data();
    }
    submitInfo.waitSemaphoreCount = (uint32_t)waitSemaphores.size();
    if (submitInfo.waitSemaphoreCount)
    {
        submitInfo.pWaitSemaphores = waitSemaphores.data();
    }
    submitInfo.signalSemaphoreCount = (uint32_t)signalSemaphores.size();
    submitInfo.pSignalSemaphores = signalSemaphores.data();

    VkFence vkFence = VK_NULL_HANDLE;
    if (count)
    {
        auto commandBufferImpl = static_cast<CommandBufferImpl*>(commandBuffers[0]);
        vkFence = commandBufferImpl->m_transientHeap->getCurrentFence();
        vkAPI.vkResetFences(vkAPI.m_device, 1, &vkFence);
        commandBufferImpl->m_transientHeap->advanceFence();
    }
    vkAPI.vkQueueSubmit(m_queue, 1, &submitInfo, vkFence);
    m_pendingWaitSemaphores[0] = m_semaphore;
    m_pendingWaitSemaphores[1] = VK_NULL_HANDLE;
}

void CommandQueueImpl::executeCommandBuffers(
    GfxCount count,
    ICommandBuffer* const* commandBuffers,
    IFence* fence,
    uint64_t valueToSignal
)
{
    if (count == 0 && fence == nullptr)
        return;
    queueSubmitImpl(count, commandBuffers, fence, valueToSignal);
}

} // namespace rhi::vk
