#include "debug-resource-heap.h"
#include "debug-helper-functions.h"

namespace rhi::debug {

const ResourceHeapDesc& DebugResourceHeap::getDesc()
{
    SLANG_RHI_DEBUG_API(IResourceHeap, getDesc);
    return baseObject->getDesc();
}

Result DebugResourceHeap::getNativeHandle(NativeHandle* outHandle)
{
    SLANG_RHI_DEBUG_API(IResourceHeap, getNativeHandle);

    if (!outHandle)
    {
        RHI_VALIDATION_ERROR("'outHandle' must not be null.");
        return SLANG_E_INVALID_ARG;
    }

    return baseObject->getNativeHandle(outHandle);
}

} // namespace rhi::debug
