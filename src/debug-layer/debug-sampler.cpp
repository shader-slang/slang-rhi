#include "debug-sampler.h"
#include "debug-helper-functions.h"

namespace rhi::debug {

const SamplerDesc& DebugSampler::getDesc()
{
    return baseObject->getDesc();
}

Result DebugSampler::getNativeHandle(NativeHandle* outHandle)
{
    SLANG_RHI_API_FUNC;
    return baseObject->getNativeHandle(outHandle);
}

} // namespace rhi::debug
