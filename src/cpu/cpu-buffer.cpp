#include "cpu-buffer.h"
#include "cpu-device.h"

namespace rhi::cpu {

BufferImpl::~BufferImpl()
{
    if (m_data)
    {
        std::free(m_data);
    }
}

DeviceAddress BufferImpl::getDeviceAddress()
{
    return (DeviceAddress)m_data;
}

Result DeviceImpl::createBuffer(const BufferDesc& descIn, const void* initData, IBuffer** outBuffer)
{
    BufferDesc desc = fixupBufferDesc(descIn);
    RefPtr<BufferImpl> buffer = new BufferImpl(desc);
    buffer->m_data = (uint8_t*)std::malloc(desc.size);
    if (!buffer->m_data)
    {
        return SLANG_E_OUT_OF_MEMORY;
    }
    if (initData)
    {
        std::memcpy(buffer->m_data, initData, desc.size);
    }
    returnComPtr(outBuffer, buffer);
    return SLANG_OK;
}

Result DeviceImpl::mapBuffer(IBuffer* buffer, CpuAccessMode mode, void** outData)
{
    *outData = checked_cast<BufferImpl*>(buffer)->m_data;
    return SLANG_OK;
}

Result DeviceImpl::unmapBuffer(IBuffer* buffer)
{
    SLANG_UNUSED(buffer);
    return SLANG_OK;
}

Result DeviceImpl::readBuffer(IBuffer* buffer, Offset offset, Size size, ISlangBlob** outBlob)
{
    BufferImpl* bufferImpl = checked_cast<BufferImpl*>(buffer);
    auto blob = OwnedBlob::create(size);
    if (offset + size > bufferImpl->m_desc.size)
    {
        return SLANG_FAIL;
    }
    std::memcpy((void*)blob->getBufferPointer(), bufferImpl->m_data + offset, size);
    returnComPtr(outBlob, blob);
    return SLANG_OK;
}

} // namespace rhi::cpu
