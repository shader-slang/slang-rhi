#include "debug-texture.h"
#include "debug-helper-functions.h"

namespace rhi::debug {

const TextureDesc& DebugTexture::getDesc()
{
    SLANG_RHI_API_FUNC;
    return baseObject->getDesc();
}

Result DebugTexture::getNativeHandle(NativeHandle* outHandle)
{
    SLANG_RHI_API_FUNC;
    return baseObject->getNativeHandle(outHandle);
}

Result DebugTexture::getSharedHandle(NativeHandle* outHandle)
{
    SLANG_RHI_API_FUNC;
    return baseObject->getSharedHandle(outHandle);
}

} // namespace rhi::debug
