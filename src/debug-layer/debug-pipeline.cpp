#include "debug-pipeline.h"
#include "debug-helper-functions.h"

namespace rhi::debug {

Result DebugPipeline::getNativeHandle(NativeHandle* outHandle)
{
    SLANG_RHI_API_FUNC;
    return baseObject->getNativeHandle(outHandle);
}

} // namespace rhi::debug
