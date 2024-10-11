#include "wgpu-command-queue.h"
#include "wgpu-command-buffer.h"
#include "wgpu-device.h"

namespace rhi::wgpu {

CommandQueueImpl::CommandQueueImpl(DeviceImpl* device, QueueType type)
    : CommandQueue(device, type)
{
    m_queue = m_device->m_ctx.api.wgpuDeviceGetQueue(m_device->m_ctx.device);
}

CommandQueueImpl::~CommandQueueImpl()
{
    if (m_queue)
    {
        m_device->m_ctx.api.wgpuQueueRelease(m_queue);
    }
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
    //     waitInfo.fence = checked_cast<FenceImpl*>(fences[i]);
    //     waitInfo.waitValue = waitValues[i];
    //     m_pendingWaitFences.push_back(waitInfo);
    // }
    return SLANG_OK;
}

void CommandQueueImpl::submit(
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
        buffers[i] = checked_cast<CommandBufferImpl*>(commandBuffers[i])->m_commandBuffer;
    }

    m_device->m_ctx.api.wgpuQueueSubmit(m_queue, buffers.size(), buffers.data());

    // TODO signal fence
    // m_device->m_ctx.api.wgpuQueueOnSubmittedWorkDone
}

Result DeviceImpl::getQueue(QueueType type, ICommandQueue** outQueue)
{
    if (type != QueueType::Graphics)
        return SLANG_FAIL;
    m_queue->establishStrongReferenceToDevice();
    returnComPtr(outQueue, m_queue);
    return SLANG_OK;
}

} // namespace rhi::wgpu
