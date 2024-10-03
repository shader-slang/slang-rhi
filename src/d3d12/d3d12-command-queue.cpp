#include "d3d12-command-queue.h"
#include "d3d12-command-buffer.h"
#include "d3d12-device.h"
#include "d3d12-fence.h"

#include "core/short_vector.h"

namespace rhi::d3d12 {

CommandQueueImpl::CommandQueueImpl(DeviceImpl* device, QueueType type)
    : CommandQueue(device, type)
{
}

CommandQueueImpl::~CommandQueueImpl()
{
    waitOnHost();
    CloseHandle(globalWaitHandle);
    m_device->m_queueIndexAllocator.free((int)m_queueIndex, 1);
}

Result CommandQueueImpl::init(uint32_t queueIndex)
{
    m_queueIndex = queueIndex;
    m_d3dDevice = m_device->m_device;
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    SLANG_RETURN_ON_FAIL(m_d3dDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(m_d3dQueue.writeRef())));
    SLANG_RETURN_ON_FAIL(m_d3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(m_fence.writeRef())));
    globalWaitHandle =
        CreateEventEx(nullptr, nullptr, CREATE_EVENT_INITIAL_SET | CREATE_EVENT_MANUAL_RESET, EVENT_ALL_ACCESS);
    return SLANG_OK;
}

void CommandQueueImpl::executeCommandBuffers(
    GfxCount count,
    ICommandBuffer* const* commandBuffers,
    IFence* fence,
    uint64_t valueToSignal
)
{
    short_vector<ID3D12CommandList*> commandLists;
    for (GfxCount i = 0; i < count; i++)
    {
        auto cmdImpl = static_cast<CommandBufferImpl*>(commandBuffers[i]);
        commandLists.push_back(cmdImpl->m_cmdList);
    }
    if (count > 0)
    {
        m_d3dQueue->ExecuteCommandLists((UINT)count, commandLists.data());

        m_fenceValue++;

        for (GfxCount i = 0; i < count; i++)
        {
            if (i > 0 && commandBuffers[i] == commandBuffers[i - 1])
                continue;
            auto cmdImpl = static_cast<CommandBufferImpl*>(commandBuffers[i]);
            auto transientHeap = cmdImpl->m_transientHeap;
            auto& waitInfo = transientHeap->getQueueWaitInfo(m_queueIndex);
            waitInfo.waitValue = m_fenceValue;
            waitInfo.fence = m_fence;
            waitInfo.queue = m_d3dQueue;
        }
    }

    if (fence)
    {
        auto fenceImpl = static_cast<FenceImpl*>(fence);
        m_d3dQueue->Signal(fenceImpl->m_fence.get(), valueToSignal);
    }
}

void CommandQueueImpl::waitOnHost()
{
    m_fenceValue++;
    m_d3dQueue->Signal(m_fence, m_fenceValue);
    ResetEvent(globalWaitHandle);
    m_fence->SetEventOnCompletion(m_fenceValue, globalWaitHandle);
    WaitForSingleObject(globalWaitHandle, INFINITE);
}

Result CommandQueueImpl::waitForFenceValuesOnDevice(GfxCount fenceCount, IFence** fences, uint64_t* waitValues)
{
    for (GfxCount i = 0; i < fenceCount; ++i)
    {
        auto fenceImpl = static_cast<FenceImpl*>(fences[i]);
        m_d3dQueue->Wait(fenceImpl->m_fence.get(), waitValues[i]);
    }
    return SLANG_OK;
}

Result CommandQueueImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::D3D12CommandQueue;
    outHandle->value = (uint64_t)m_d3dQueue.get();
    return SLANG_OK;
}

} // namespace rhi::d3d12
