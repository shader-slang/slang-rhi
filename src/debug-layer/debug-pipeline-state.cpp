#include "debug-pipeline-state.h"
#include "debug-helper-functions.h"

namespace rhi::debug {

Result DebugPipelineState::getNativeHandle(InteropHandle* outHandle)
{
    SLANG_RHI_API_FUNC;
    return baseObject->getNativeHandle(outHandle);
}

} // namespace rhi::debug
