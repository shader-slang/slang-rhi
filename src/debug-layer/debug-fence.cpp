#include "debug-fence.h"
#include "debug-helper-functions.h"

namespace rhi::debug {

Result DebugFence::getNativeHandle(NativeHandle* outHandle)
{
    SLANG_RHI_API_FUNC;
    return baseObject->getNativeHandle(outHandle);
}

Result DebugFence::getSharedHandle(NativeHandle* outHandle)
{
    SLANG_RHI_API_FUNC;
    return baseObject->getSharedHandle(outHandle);
}

Result DebugFence::getCurrentValue(uint64_t* outValue)
{
    SLANG_RHI_API_FUNC;
    return baseObject->getCurrentValue(outValue);
}

Result DebugFence::setCurrentValue(uint64_t value)
{
    SLANG_RHI_API_FUNC;
    if (value < maxValueToSignal)
    {
        RHI_VALIDATION_ERROR_FORMAT(
            "Cannot set fence value (%d) to lower than pending signal value (%d) on the fence.",
            value,
            maxValueToSignal
        );
    }
    return baseObject->setCurrentValue(value);
}

} // namespace rhi::debug
