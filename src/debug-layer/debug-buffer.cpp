#include "debug-buffer.h"
#include "debug-helper-functions.h"

namespace rhi::debug {

const BufferDesc& DebugBuffer::getDesc()
{
    SLANG_RHI_API_FUNC;
    return baseObject->getDesc();
}

DeviceAddress DebugBuffer::getDeviceAddress()
{
    SLANG_RHI_API_FUNC;
    return baseObject->getDeviceAddress();
}

Result DebugBuffer::getNativeHandle(NativeHandle* outHandle)
{
    SLANG_RHI_API_FUNC;
    return baseObject->getNativeHandle(outHandle);
}

Result DebugBuffer::getSharedHandle(NativeHandle* outHandle)
{
    SLANG_RHI_API_FUNC;
    return baseObject->getSharedHandle(outHandle);
}

Result DebugBuffer::map(MemoryRange* rangeToRead, void** outPointer)
{
    SLANG_RHI_API_FUNC;
    return baseObject->map(rangeToRead, outPointer);
}

Result DebugBuffer::unmap(MemoryRange* writtenRange)
{
    return baseObject->unmap(writtenRange);
}

} // namespace rhi::debug
