#include "wgpu-buffer.h"
#include "wgpu-command.h"
#include "wgpu-device.h"
#include "wgpu-utils.h"

#include "core/deferred.h"

#include <cstring>

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

DeviceAddress BufferImpl::getDeviceAddress()
{
    return 0;
}

Result DeviceImpl::createBuffer(const BufferDesc& desc_, const void* initData, IBuffer** outBuffer)
{
    BufferDesc desc = fixupBufferDesc(desc_);

    RefPtr<BufferImpl> buffer = new BufferImpl(this, desc);
    WGPUBufferDescriptor bufferDesc = {};
    // Buffer writes, mappings and copies must be multiples of 4 bytes.
    bufferDesc.size = math::calcAligned2(desc.size, 4);
    bufferDesc.usage = translateBufferUsage(desc.usage);
    if (desc.memoryType == MemoryType::Upload)
    {
        // Upload buffers keep a CPU copy that is written to the GPU buffer when unmapped (see BufferImpl).
        // This allows any usage, while mapping for writing is only allowed together with CopySrc.
        bufferDesc.usage |= WGPUBufferUsage_CopyDst;
        buffer->m_uploadData = std::make_unique<uint8_t[]>(bufferDesc.size);
    }
    else if (desc.memoryType == MemoryType::ReadBack)
    {
        // WGPU only allows MapRead together with CopyDst.
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
        *outBuffer = nullptr;
        return SLANG_FAIL;
    }

    if (initData)
    {
        if (buffer->m_uploadData)
            std::memcpy(buffer->m_uploadData.get(), initData, desc.size);
        // Queue writes take effect before any later submission, so there is no need to wait.
        writeBuffer(buffer, 0, desc.size, initData);
    }

    returnComPtr(outBuffer, buffer);
    return SLANG_OK;
}

Result DeviceImpl::createBufferFromNativeHandle(NativeHandle handle, const BufferDesc& desc, IBuffer** outBuffer)
{
    if (handle.type != NativeHandleType::WGPUBuffer || handle.value == 0)
    {
        *outBuffer = nullptr;
        return SLANG_E_INVALID_HANDLE;
    }

    BufferDesc fixedDesc = fixupBufferDesc(desc);
    RefPtr<BufferImpl> buffer = new BufferImpl(this, fixedDesc);
    buffer->m_buffer = (WGPUBuffer)handle.value;
    m_ctx.api.wgpuBufferAddRef(buffer->m_buffer);

    returnComPtr(outBuffer, buffer);
    return SLANG_OK;
}

Result DeviceImpl::createBufferFromSharedHandle(NativeHandle handle, const BufferDesc& desc, IBuffer** outBuffer)
{
    *outBuffer = nullptr;
    return SLANG_E_NOT_AVAILABLE;
}

Result DeviceImpl::mapBuffer(IBuffer* buffer, CpuAccessMode mode, void** outData)
{
    BufferImpl* bufferImpl = checked_cast<BufferImpl*>(buffer);

    if (bufferImpl->m_uploadData)
    {
        *outData = bufferImpl->m_uploadData.get();
        return SLANG_OK;
    }

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
    size_t size = math::calcAligned2(bufferImpl->m_desc.size, 4);

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
    WGPUWaitStatus waitStatus = wgpu::wait(m_ctx, future);
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
    if (bufferImpl->m_uploadData)
    {
        writeBuffer(bufferImpl, 0, bufferImpl->m_desc.size, bufferImpl->m_uploadData.get());
        return SLANG_OK;
    }
    m_ctx.api.wgpuBufferUnmap(bufferImpl->m_buffer);
    return SLANG_OK;
}

Result DeviceImpl::writeUploadBuffer(Buffer* buffer, Offset offset, Size size, const void* data)
{
    BufferImpl* bufferImpl = checked_cast<BufferImpl*>(buffer);
    SLANG_RHI_ASSERT(bufferImpl->m_uploadData);
    SLANG_RHI_ASSERT(offset + size <= bufferImpl->m_desc.size);
    std::memcpy(bufferImpl->m_uploadData.get() + offset, data, size);
    // Queue writes need 4-byte aligned offsets and sizes. Write the enclosing aligned range from the
    // CPU copy, so that neighboring bytes keep their current values.
    Offset begin = offset & ~Offset(3);
    Offset end = math::calcAligned2(offset + size, 4);
    writeBuffer(bufferImpl, begin, end - begin, bufferImpl->m_uploadData.get() + begin);
    return SLANG_OK;
}

void DeviceImpl::writeBuffer(BufferImpl* buffer, Offset offset, Size size, const void* data)
{
    SLANG_RHI_ASSERT((offset & 3) == 0);
    WGPUQueue queue = m_queue->m_queue;
    Size alignedSize = size & ~Size(3);
    if (alignedSize > 0)
    {
        m_ctx.api.wgpuQueueWriteBuffer(queue, buffer->m_buffer, offset, data, alignedSize);
    }
    if (alignedSize < size)
    {
        // Pad the last bytes, which stay within the buffer as its size is a multiple of 4.
        uint8_t tail[4] = {};
        std::memcpy(tail, static_cast<const uint8_t*>(data) + alignedSize, size - alignedSize);
        m_ctx.api.wgpuQueueWriteBuffer(queue, buffer->m_buffer, offset + alignedSize, tail, sizeof(tail));
    }
}

} // namespace rhi::wgpu
