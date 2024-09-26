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

Result BufferImpl::map(BufferRange* rangeToRead, void** outPointer)
{
    if (m_isMapped)
    {
        return SLANG_FAIL;
    }

    auto callback = [](WGPUBufferMapAsyncStatus status, void* data)
    {
        BufferImpl* buffer = static_cast<BufferImpl*>(data);
        if (status == WGPUBufferMapAsyncStatus_Success)
        {
            buffer->m_isMapped = true;
        }
        else
        {
            buffer->m_isMapped = false;
        }
    };

    size_t offset = rangeToRead ? rangeToRead->offset : 0;
    size_t size = rangeToRead ? rangeToRead->size : m_desc.size;

    m_device->m_ctx.api.wgpuBufferMapAsync(m_buffer, m_mapMode, offset, size, callback, this);
    if (!m_isMapped)
    {
        return SLANG_FAIL;
    }
    *outPointer = m_device->m_ctx.api.wgpuBufferGetMappedRange(m_buffer, offset, size);
    return SLANG_OK;
}

Result BufferImpl::unmap(BufferRange* writtenRange)
{
    if (!m_isMapped)
    {
        return SLANG_FAIL;
    }

    m_device->m_ctx.api.wgpuBufferUnmap(m_buffer);
    m_isMapped = false;
    return SLANG_OK;
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
        buffer->m_mapMode = WGPUMapMode_Write;
    }
    else if (desc.memoryType == MemoryType::ReadBack)
    {
        bufferDesc.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
        buffer->m_mapMode = WGPUMapMode_Read;
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
        // TODO: we should switch to the new async API
        {
            WGPUQueueWorkDoneStatus status = WGPUQueueWorkDoneStatus_Unknown;
            m_ctx.api.wgpuQueueOnSubmittedWorkDone(
                queue,
                [](WGPUQueueWorkDoneStatus status, void* userdata) { *(WGPUQueueWorkDoneStatus*)userdata = status; },
                &status
            );
            while (status == WGPUQueueWorkDoneStatus_Unknown)
            {
                m_ctx.api.wgpuDeviceTick(m_ctx.device);
            }
            if (status != WGPUQueueWorkDoneStatus_Success)
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

} // namespace rhi::wgpu
