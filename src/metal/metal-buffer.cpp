#include "metal-buffer.h"
#include "metal-util.h"

namespace rhi::metal {

BufferImpl::BufferImpl(const IBuffer::Desc& desc, DeviceImpl* device)
    : Parent(desc)
    , m_device(device)
{
}

BufferImpl::~BufferImpl() {}

DeviceAddress BufferImpl::getDeviceAddress()
{
    return m_buffer->gpuAddress();
}

Result BufferImpl::getNativeResourceHandle(InteropHandle* outHandle)
{
    outHandle->api = InteropHandleAPI::Metal;
    outHandle->handleValue = reinterpret_cast<intptr_t>(m_buffer.get());
    return SLANG_OK;
}

Result BufferImpl::getSharedHandle(InteropHandle* outHandle)
{
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

Result BufferImpl::setDebugName(const char* name)
{
    Parent::setDebugName(name);
    m_buffer->addDebugMarker(MetalUtil::createString(name).get(), NS::Range(0, m_desc.sizeInBytes));
    return SLANG_OK;
}

} // namespace rhi::metal
