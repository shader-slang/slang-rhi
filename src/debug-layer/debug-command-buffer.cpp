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

    if (!outHandle)
    {
        RHI_VALIDATION_ERROR("'outHandle' must not be null.");
        return SLANG_E_INVALID_ARG;
    }

    return baseObject->getNativeHandle(outHandle);
}

} // namespace rhi::debug
