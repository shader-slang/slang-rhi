#include "debug-texture.h"
#include "debug-helper-functions.h"

namespace rhi::debug {

TextureDesc* DebugTexture::getDesc()
{
    SLANG_RHI_API_FUNC;
    return baseObject->getDesc();
}

Result DebugTexture::getNativeResourceHandle(InteropHandle* outHandle)
{
    SLANG_RHI_API_FUNC;
    return baseObject->getNativeResourceHandle(outHandle);
}

Result DebugTexture::getSharedHandle(InteropHandle* outHandle)
{
    SLANG_RHI_API_FUNC;
    return baseObject->getSharedHandle(outHandle);
}

} // namespace rhi::debug
