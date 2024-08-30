#include "d3d11-buffer.h"

namespace rhi::d3d11 {

SLANG_NO_THROW DeviceAddress SLANG_MCALL BufferImpl::getDeviceAddress()
{
    return 0;
}

SLANG_NO_THROW Result SLANG_MCALL BufferImpl::map(MemoryRange* rangeToRead, void** outPointer)
{
    SLANG_UNUSED(rangeToRead);
    SLANG_UNUSED(outPointer);
    return SLANG_FAIL;
}

SLANG_NO_THROW Result SLANG_MCALL BufferImpl::unmap(MemoryRange* writtenRange)
{
    SLANG_UNUSED(writtenRange);
    return SLANG_FAIL;
}

} // namespace rhi::d3d11
