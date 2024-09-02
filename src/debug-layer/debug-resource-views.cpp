#include "debug-resource-views.h"
#include "debug-helper-functions.h"

namespace rhi::debug {

IResourceView::Desc* DebugResourceView::getViewDesc()
{
    SLANG_RHI_API_FUNC;

    return baseObject->getViewDesc();
}

Result DebugResourceView::getNativeHandle(NativeHandle* outHandle)
{
    SLANG_RHI_API_FUNC;

    return baseObject->getNativeHandle(outHandle);
}

DeviceAddress DebugAccelerationStructure::getDeviceAddress()
{
    SLANG_RHI_API_FUNC;

    return baseObject->getDeviceAddress();
}

Result DebugAccelerationStructure::getNativeHandle(NativeHandle* outHandle)
{
    SLANG_RHI_API_FUNC;

    return baseObject->getNativeHandle(outHandle);
}

IResourceView::Desc* DebugAccelerationStructure::getViewDesc()
{
    SLANG_RHI_API_FUNC;

    return baseObject->getViewDesc();
}

} // namespace rhi::debug
