#include "wgpu-command-queue.h"
#include "wgpu-command-buffer.h"
#include "wgpu-device.h"

namespace rhi::wgpu {

ICommandQueue* CommandQueueImpl::getInterface(const Guid& guid)
{
    if (guid == GUID::IID_ISlangUnknown || guid == GUID::IID_ICommandQueue)
        return static_cast<ICommandQueue*>(this);
    return nullptr;
}

CommandQueueImpl::~CommandQueueImpl()
{
    if (m_queue)
    {
        m_device->m_ctx.api.wgpuQueueRelease(m_queue);
    }
}

const CommandQueueImpl::Desc& CommandQueueImpl::getDesc()
{
    return m_desc;
}

Result CommandQueueImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::WGPUQueue;
    outHandle->value = (uint64_t)m_queue;
    return SLANG_OK;
}

void CommandQueueImpl::waitOnHost()
{
    // Wait for the command buffer to finish executing
    // TODO: we should switch to the new async API
    {
        WGPUQueueWorkDoneStatus status = WGPUQueueWorkDoneStatus_Unknown;
        m_device->m_ctx.api.wgpuQueueOnSubmittedWorkDone(
            m_queue,
            [](WGPUQueueWorkDoneStatus status, void* userdata) { *(WGPUQueueWorkDoneStatus*)userdata = status; },
            &status
        );
        while (status == WGPUQueueWorkDoneStatus_Unknown)
        {
            m_device->m_ctx.api.wgpuDeviceTick(m_device->m_ctx.device);
        }
    }
}

Result CommandQueueImpl::waitForFenceValuesOnDevice(GfxCount fenceCount, IFence** fences, uint64_t* waitValues)
{
    // for (GfxCount i = 0; i < fenceCount; ++i)
    // {
    //     FenceWaitInfo waitInfo;
    //     waitInfo.fence = static_cast<FenceImpl*>(fences[i]);
    //     waitInfo.waitValue = waitValues[i];
    //     m_pendingWaitFences.push_back(waitInfo);
    // }
    return SLANG_OK;
}

void CommandQueueImpl::executeCommandBuffers(
    GfxCount count,
    ICommandBuffer* const* commandBuffers,
    IFence* fence,
    uint64_t valueToSignal
)
{
    if (count == 0 && fence == nullptr)
    {
        return;
    }

    short_vector<WGPUCommandBuffer, 16> buffers;
    buffers.resize(count);
    for (GfxIndex i = 0; i < count; ++i)
    {
        buffers[i] = static_cast<CommandBufferImpl*>(commandBuffers[i])->m_commandBuffer;
    }

    m_device->m_ctx.api.wgpuQueueSubmit(m_queue, buffers.size(), buffers.data());

    // TODO signal fence
    // m_device->m_ctx.api.wgpuQueueOnSubmittedWorkDone
}

Result DeviceImpl::createCommandQueue(const ICommandQueue::Desc& desc, ICommandQueue** outQueue)
{
    RefPtr<CommandQueueImpl> queue = new CommandQueueImpl();
    queue->m_desc = desc;
    queue->m_device = this;
    queue->m_queue = m_ctx.api.wgpuDeviceGetQueue(m_ctx.device);
    if (!queue->m_queue)
    {
        return SLANG_FAIL;
    }
    returnComPtr(outQueue, queue);
    return SLANG_OK;
}

} // namespace rhi::wgpu
