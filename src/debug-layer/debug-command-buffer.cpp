#include "debug-command-buffer.h"
#include "debug-helper-functions.h"

namespace rhi::debug {

const CommandBufferDesc& DebugCommandBuffer::getDesc()
{
    return baseObject->getDesc();
}

Result DebugCommandBuffer::getNativeHandle(NativeHandle* outHandle)
{
    SLANG_RHI_DEBUG_API(ICommandBuffer, getNativeHandle);

    return baseObject->getNativeHandle(outHandle);
}

} // namespace rhi::debug
