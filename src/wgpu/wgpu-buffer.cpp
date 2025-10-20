#include "wgpu-buffer.h"
#include "wgpu-device.h"
#include "wgpu-utils.h"

#include "core/deferred.h"

namespace rhi::wgpu {

BufferImpl::BufferImpl(Device* device, const BufferDesc& desc)
    : Buffer(device, desc)
{
}

BufferImpl::~BufferImpl()
{
    if (m_buffer)
    {
        getDevice<DeviceImpl>()->m_ctx.api.wgpuBufferRelease(m_buffer);
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

Result DeviceImpl::createBuffer(const BufferDesc& desc_, const void* initData, IBuffer** outBuffer)
{
    BufferDesc desc = fixupBufferDesc(desc_);

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

    bufferDesc.label = translateString(desc.label);
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
            WGPUQueueWorkDoneStatus status = WGPUQueueWorkDoneStatus(0);
            WGPUQueueWorkDoneCallbackInfo callbackInfo = {};
            callbackInfo.mode = WGPUCallbackMode_WaitAnyOnly;
            callbackInfo.callback = [](WGPUQueueWorkDoneStatus status_, void* userdata1, void* userdata2)
            {
                *(WGPUQueueWorkDoneStatus*)userdata1 = status_;
            };
            callbackInfo.userdata1 = &status;
            WGPUFuture future = m_ctx.api.wgpuQueueOnSubmittedWorkDone(queue, callbackInfo);
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

Result DeviceImpl::createBufferFromNativeHandle(NativeHandle handle, const BufferDesc& desc, IBuffer** outBuffer)
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

    WGPUMapAsyncStatus status = WGPUMapAsyncStatus(0);
    WGPUBufferMapCallbackInfo callbackInfo = {};
    callbackInfo.mode = WGPUCallbackMode_WaitAnyOnly;
    callbackInfo.callback = [](WGPUMapAsyncStatus status_, WGPUStringView message, void* userdata1, void* userdata2)
    {
        *(WGPUMapAsyncStatus*)userdata1 = status_;
        if (status_ != WGPUMapAsyncStatus_Success)
        {
            static_cast<DeviceImpl*>(userdata2)->reportError("wgpuBufferMapAsync", message);
        }
    };
    callbackInfo.userdata1 = &status;
    callbackInfo.userdata2 = this;
    WGPUFuture future = m_ctx.api.wgpuBufferMapAsync(bufferImpl->m_buffer, mapMode, offset, size, callbackInfo);
    WGPUFutureWaitInfo futures[1] = {{future}};
    uint64_t timeoutNS = UINT64_MAX;
    WGPUWaitStatus waitStatus =
        m_ctx.api.wgpuInstanceWaitAny(m_ctx.instance, SLANG_COUNT_OF(futures), futures, timeoutNS);
    if (waitStatus != WGPUWaitStatus_Success || status != WGPUMapAsyncStatus_Success)
    {
        return SLANG_FAIL;
    }

    if (mapMode == WGPUMapMode_Read)
        *outData = const_cast<void*>(m_ctx.api.wgpuBufferGetConstMappedRange(bufferImpl->m_buffer, offset, size));
    else
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
