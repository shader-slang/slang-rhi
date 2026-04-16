#include "debug-fence.h"
#include "debug-helper-functions.h"

namespace rhi::debug {

Result DebugFence::getNativeHandle(NativeHandle* outHandle)
{
    SLANG_RHI_DEBUG_API(IFence, getNativeHandle);

    if (!outHandle)
    {
        RHI_VALIDATION_ERROR("'outHandle' must not be null.");
        return SLANG_E_INVALID_ARG;
    }

    return baseObject->getNativeHandle(outHandle);
}

Result DebugFence::getSharedHandle(NativeHandle* outHandle)
{
    SLANG_RHI_DEBUG_API(IFence, getSharedHandle);

    if (!outHandle)
    {
        RHI_VALIDATION_ERROR("'outHandle' must not be null.");
        return SLANG_E_INVALID_ARG;
    }

    return baseObject->getSharedHandle(outHandle);
}

Result DebugFence::getCurrentValue(uint64_t* outValue)
{
    SLANG_RHI_DEBUG_API(IFence, getCurrentValue);

    if (!outValue)
    {
        RHI_VALIDATION_ERROR("'outValue' must not be null.");
        return SLANG_E_INVALID_ARG;
    }

    return baseObject->getCurrentValue(outValue);
}

Result DebugFence::setCurrentValue(uint64_t value)
{
    SLANG_RHI_DEBUG_API(IFence, setCurrentValue);

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
