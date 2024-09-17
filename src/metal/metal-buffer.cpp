#include "metal-buffer.h"
#include "metal-util.h"

namespace rhi::metal {

BufferImpl::BufferImpl(const BufferDesc& desc)
    : Buffer(desc)
{
}

BufferImpl::~BufferImpl() {}

DeviceAddress BufferImpl::getDeviceAddress()
{
    return m_buffer->gpuAddress();
}

Result BufferImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::MTLBuffer;
    outHandle->value = (uint64_t)m_buffer.get();
    return SLANG_OK;
}

Result BufferImpl::getSharedHandle(NativeHandle* outHandle)
{
    *outHandle = {};
    return SLANG_E_NOT_AVAILABLE;
}

Result BufferImpl::map(MemoryRange* rangeToRead, void** outPointer)
{
    *outPointer = m_buffer->contents();
    return SLANG_OK;
}

Result BufferImpl::unmap(MemoryRange* writtenRange)
{
    return SLANG_OK;
}

} // namespace rhi::metal
