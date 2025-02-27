#include "wgpu-buffer.h"
#include "wgpu-device.h"
#include "wgpu-util.h"

#include "core/deferred.h"

namespace rhi::wgpu {

BufferImpl::BufferImpl(DeviceImpl* device, const BufferDesc& desc)
    : Buffer(desc)
    , m_device(device)
{
}

BufferImpl::~BufferImpl()
{
    if (m_buffer)
    {
        m_device->m_ctx.api.wgpuBufferRelease(m_buffer);
    }
}

DeviceAddress BufferImpl::getDeviceAddress()
{
    return 0;
}

Result BufferImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::WGPUBuffer;
    outHandle->value = (uint64_t)m_buffer;
    return SLANG_OK;
}

Result BufferImpl::getSharedHandle(NativeHandle* outHandle)
{
    *outHandle = {};
    return SLANG_E_NOT_AVAILABLE;
}

Result DeviceImpl::createBuffer(const BufferDesc& desc, const void* initData, IBuffer** outBuffer)
{
    RefPtr<BufferImpl> buffer = new BufferImpl(this, desc);
    WGPUBufferDescriptor bufferDesc = {};
    bufferDesc.size = desc.size;
    bufferDesc.usage = translateBufferUsage(desc.usage);
    // TODO:
    // Warn if other usage flags if memory type is Upload/ReadBack.
    // WGPU only allows MapWrite+CopySrc, MapRead+CopyDst exclusively.
    if (desc.memoryType == MemoryType::Upload)
    {
        bufferDesc.usage = WGPUBufferUsage_MapWrite | WGPUBufferUsage_CopySrc;
    }
    else if (desc.memoryType == MemoryType::ReadBack)
    {
        bufferDesc.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
    }
    if (initData)
    {
        bufferDesc.usage |= WGPUBufferUsage_CopyDst;
    }

    bufferDesc.label = desc.label;
    buffer->m_buffer = m_ctx.api.wgpuDeviceCreateBuffer(m_ctx.device, &bufferDesc);
    if (!buffer->m_buffer)
    {
        return SLANG_FAIL;
    }

    if (initData)
    {
        WGPUQueue queue = m_ctx.api.wgpuDeviceGetQueue(m_ctx.device);
        m_ctx.api.wgpuQueueWriteBuffer(queue, buffer->m_buffer, 0, initData, desc.size);
        SLANG_RHI_DEFERRED({ m_ctx.api.wgpuQueueRelease(queue); });

        // Wait for the command buffer to finish executing
        {
            WGPUQueueWorkDoneStatus status = WGPUQueueWorkDoneStatus_Unknown;
            WGPUQueueWorkDoneCallbackInfo2 callbackInfo = {};
            callbackInfo.mode = WGPUCallbackMode_WaitAnyOnly;
            callbackInfo.callback = [](WGPUQueueWorkDoneStatus status_, void* userdata1, void* userdata2)
            { *(WGPUQueueWorkDoneStatus*)userdata1 = status_; };
            callbackInfo.userdata1 = &status;
            WGPUFuture future = m_ctx.api.wgpuQueueOnSubmittedWorkDone2(queue, callbackInfo);
            constexpr size_t futureCount = 1;
            WGPUFutureWaitInfo futures[futureCount] = {{future}};
            uint64_t timeoutNS = UINT64_MAX;
            WGPUWaitStatus waitStatus = m_ctx.api.wgpuInstanceWaitAny(m_ctx.instance, futureCount, futures, timeoutNS);
            if (waitStatus != WGPUWaitStatus_Success || status != WGPUQueueWorkDoneStatus_Success)
            {
                return SLANG_FAIL;
            }
        }
    }

    returnComPtr(outBuffer, buffer);
    return SLANG_OK;
}

Result DeviceImpl::createBufferFromNativeHandle(NativeHandle handle, const BufferDesc& srcDesc, IBuffer** outBuffer)
{
    return SLANG_E_NOT_IMPLEMENTED;
}

Result DeviceImpl::mapBuffer(IBuffer* buffer, CpuAccessMode mode, void** outData)
{
    BufferImpl* bufferImpl = checked_cast<BufferImpl*>(buffer);

    WGPUMapMode mapMode = WGPUMapMode_None;
    switch (mode)
    {
    case CpuAccessMode::Read:
        mapMode = WGPUMapMode_Read;
        break;
    case CpuAccessMode::Write:
        mapMode = WGPUMapMode_Write;
        break;
    }

    size_t offset = 0;
    size_t size = bufferImpl->m_desc.size;

    WGPUMapAsyncStatus status = WGPUMapAsyncStatus_Unknown;
    WGPUBufferMapCallbackInfo2 callbackInfo = {};
    callbackInfo.mode = WGPUCallbackMode_WaitAnyOnly;
    callbackInfo.callback = [](WGPUMapAsyncStatus status_, const char* message, void* userdata1, void* userdata2)
    { *(WGPUMapAsyncStatus*)userdata1 = status_; };
    callbackInfo.userdata1 = &status;
    WGPUFuture future = m_ctx.api.wgpuBufferMapAsync2(bufferImpl->m_buffer, mapMode, offset, size, callbackInfo);
    WGPUFutureWaitInfo futures[1] = {{future}};
    uint64_t timeoutNS = UINT64_MAX;
    WGPUWaitStatus waitStatus =
        m_ctx.api.wgpuInstanceWaitAny(m_ctx.instance, SLANG_COUNT_OF(futures), futures, timeoutNS);
    if (waitStatus != WGPUWaitStatus_Success || status != WGPUMapAsyncStatus_Success)
    {
        return SLANG_FAIL;
    }

    *outData = m_ctx.api.wgpuBufferGetMappedRange(bufferImpl->m_buffer, offset, size);
    return SLANG_OK;
}

Result DeviceImpl::unmapBuffer(IBuffer* buffer)
{
    BufferImpl* bufferImpl = checked_cast<BufferImpl*>(buffer);
    m_ctx.api.wgpuBufferUnmap(bufferImpl->m_buffer);
    return SLANG_OK;
}

} // namespace rhi::wgpu
