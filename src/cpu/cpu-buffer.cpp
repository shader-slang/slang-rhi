#include "cpu-buffer.h"

namespace rhi::cpu {

BufferImpl::~BufferImpl()
{
    if (m_data)
    {
        free(m_data);
    }
}

Result BufferImpl::init()
{
    m_data = malloc(m_desc.sizeInBytes);
    if (!m_data)
        return SLANG_E_OUT_OF_MEMORY;
    return SLANG_OK;
}

Result BufferImpl::setData(size_t offset, size_t size, void const* data)
{
    memcpy((char*)m_data + offset, data, size);
    return SLANG_OK;
}

SLANG_NO_THROW DeviceAddress SLANG_MCALL BufferImpl::getDeviceAddress()
{
    return (DeviceAddress)m_data;
}

SLANG_NO_THROW Result SLANG_MCALL BufferImpl::map(MemoryRange* rangeToRead, void** outPointer)
{
    SLANG_UNUSED(rangeToRead);
    if (outPointer)
        *outPointer = m_data;
    return SLANG_OK;
}

SLANG_NO_THROW Result SLANG_MCALL BufferImpl::unmap(MemoryRange* writtenRange)
{
    SLANG_UNUSED(writtenRange);
    return SLANG_OK;
}

} // namespace rhi::cpu
