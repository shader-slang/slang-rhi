#include "webgpu-buffer.h"
#include "webgpu-device.h"
#include "webgpu-utils.h"

#include "core/deferred.h"

namespace rhi::webgpu {

BufferImpl::BufferImpl(Device* device, const BufferDesc& desc)
    : Buffer(device, desc)
{
}

BufferImpl::~BufferImpl()
{
    if (m_buffer)
    {
        getDevice<DeviceImpl>()->m_ctx.api.webgpuBufferRelease(m_buffer);
    }
}

DeviceAddress BufferImpl::getDeviceAddress()
{
    return 0;
}

Result BufferImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::WebGPUBuffer;
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
    WebGPUBufferDescriptor bufferDesc = {};
    bufferDesc.size = desc.size;
    bufferDesc.usage = translateBufferUsage(desc.usage);
    // TODO:
    // Warn if other usage flags if memory type is Upload/ReadBack.
    // WebGPU only allows MapWrite+CopySrc, MapRead+CopyDst exclusively.
    if (desc.memoryType == MemoryType::Upload)
    {
        bufferDesc.usage = WebGPUBufferUsage_MapWrite | WebGPUBufferUsage_CopySrc;
    }
    else if (desc.memoryType == MemoryType::ReadBack)
    {
        bufferDesc.usage = WebGPUBufferUsage_MapRead | WebGPUBufferUsage_CopyDst;
    }
    if (initData)
    {
        bufferDesc.usage |= WebGPUBufferUsage_CopyDst;
    }

    bufferDesc.label = desc.label;
    buffer->m_buffer = m_ctx.api.webgpuDeviceCreateBuffer(m_ctx.device, &bufferDesc);
    if (!buffer->m_buffer)
    {
        return SLANG_FAIL;
    }

    if (initData)
    {
        WebGPUQueue queue = m_ctx.api.webgpuDeviceGetQueue(m_ctx.device);
        m_ctx.api.webgpuQueueWriteBuffer(queue, buffer->m_buffer, 0, initData, desc.size);
        SLANG_RHI_DEFERRED({ m_ctx.api.webgpuQueueRelease(queue); });

        // Wait for the command buffer to finish executing
        {
            WebGPUQueueWorkDoneStatus status = WebGPUQueueWorkDoneStatus_Unknown;
            WebGPUQueueWorkDoneCallbackInfo2 callbackInfo = {};
            callbackInfo.mode = WebGPUCallbackMode_WaitAnyOnly;
            callbackInfo.callback = [](WebGPUQueueWorkDoneStatus status_, void* userdata1, void* userdata2)
            { *(WebGPUQueueWorkDoneStatus*)userdata1 = status_; };
            callbackInfo.userdata1 = &status;
            WebGPUFuture future = m_ctx.api.webgpuQueueOnSubmittedWorkDone2(queue, callbackInfo);
            constexpr size_t futureCount = 1;
            WebGPUFutureWaitInfo futures[futureCount] = {{future}};
            uint64_t timeoutNS = UINT64_MAX;
            WebGPUWaitStatus waitStatus = m_ctx.api.webgpuInstanceWaitAny(m_ctx.instance, futureCount, futures, timeoutNS);
            if (waitStatus != WebGPUWaitStatus_Success || status != WebGPUQueueWorkDoneStatus_Success)
            {
                return SLANG_FAIL;
            }
        }
    }

    returnComPtr(outBuffer, buffer);
    return SLANG_OK;
}

Result DeviceImpl::createBufferFromNativeHandle(NativeHandle handle, const BufferDesc& desc, IBuffer** outBuffer)
{
    return SLANG_E_NOT_IMPLEMENTED;
}

Result DeviceImpl::mapBuffer(IBuffer* buffer, CpuAccessMode mode, void** outData)
{
    BufferImpl* bufferImpl = checked_cast<BufferImpl*>(buffer);

    WebGPUMapMode mapMode = WebGPUMapMode_None;
    switch (mode)
    {
    case CpuAccessMode::Read:
        mapMode = WebGPUMapMode_Read;
        break;
    case CpuAccessMode::Write:
        mapMode = WebGPUMapMode_Write;
        break;
    }

    size_t offset = 0;
    size_t size = bufferImpl->m_desc.size;

    WebGPUMapAsyncStatus status = WebGPUMapAsyncStatus_Unknown;
    WebGPUBufferMapCallbackInfo2 callbackInfo = {};
    callbackInfo.mode = WebGPUCallbackMode_WaitAnyOnly;
    callbackInfo.callback = [](WebGPUMapAsyncStatus status_, const char* message, void* userdata1, void* userdata2)
    {
        *(WebGPUMapAsyncStatus*)userdata1 = status_;
        if (status_ != WebGPUMapAsyncStatus_Success)
            fprintf(stderr, "MapAsync wait failed with message: %s\n", message);
    };
    callbackInfo.userdata1 = &status;
    WebGPUFuture future = m_ctx.api.webgpuBufferMapAsync2(bufferImpl->m_buffer, mapMode, offset, size, callbackInfo);
    WebGPUFutureWaitInfo futures[1] = {{future}};
    uint64_t timeoutNS = UINT64_MAX;
    WebGPUWaitStatus waitStatus =
        m_ctx.api.webgpuInstanceWaitAny(m_ctx.instance, SLANG_COUNT_OF(futures), futures, timeoutNS);
    if (waitStatus != WebGPUWaitStatus_Success || status != WebGPUMapAsyncStatus_Success)
    {
        return SLANG_FAIL;
    }

    if (mapMode == WebGPUMapMode_Read)
        *outData = const_cast<void*>(m_ctx.api.webgpuBufferGetConstMappedRange(bufferImpl->m_buffer, offset, size));
    else
        *outData = m_ctx.api.webgpuBufferGetMappedRange(bufferImpl->m_buffer, offset, size);
    return SLANG_OK;
}

Result DeviceImpl::unmapBuffer(IBuffer* buffer)
{
    BufferImpl* bufferImpl = checked_cast<BufferImpl*>(buffer);
    m_ctx.api.webgpuBufferUnmap(bufferImpl->m_buffer);
    return SLANG_OK;
}

} // namespace rhi::webgpu
