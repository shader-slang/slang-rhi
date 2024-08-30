#include "debug-sampler-state.h"
#include "debug-helper-functions.h"

namespace rhi::debug {

Result DebugSamplerState::getNativeHandle(InteropHandle* outNativeHandle)
{
    SLANG_RHI_API_FUNC;

    return baseObject->getNativeHandle(outNativeHandle);
}

} // namespace rhi::debug
