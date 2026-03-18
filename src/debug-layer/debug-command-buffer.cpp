#include "debug-command-buffer.h"
#include "debug-helper-functions.h"

namespace rhi::debug {

Result DebugCommandBuffer::getNativeHandle(NativeHandle* outHandle)
{
    SLANG_RHI_DEBUG_API(ICommandBuffer, getNativeHandle);

    return baseObject->getNativeHandle(outHandle);
}

void DebugCommandBuffer::setLabel(const char* label)
{
    SLANG_RHI_DEBUG_API(ICommandBuffer, setLabel);
    baseObject->setLabel(label);
}

} // namespace rhi::debug
