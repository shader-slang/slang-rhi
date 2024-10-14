#include "debug-command-buffer.h"
#include "debug-helper-functions.h"

namespace rhi::debug {

Result DebugCommandBuffer::getNativeHandle(NativeHandle* outHandle)
{
    SLANG_RHI_API_FUNC;
    return baseObject->getNativeHandle(outHandle);
}

} // namespace rhi::debug
