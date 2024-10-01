#include "debug-texture-view.h"
#include "debug-helper-functions.h"

namespace rhi::debug {

Result DebugTextureView::getNativeHandle(NativeHandle* outHandle)
{
    SLANG_RHI_API_FUNC;

    return baseObject->getNativeHandle(outHandle);
}

AccelerationStructureHandle DebugAccelerationStructure::getHandle()
{
    SLANG_RHI_API_FUNC;

    return baseObject->getHandle();
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

} // namespace rhi::debug
