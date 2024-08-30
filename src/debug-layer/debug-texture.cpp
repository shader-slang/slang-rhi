#include "debug-texture.h"
#include "debug-helper-functions.h"

namespace rhi::debug {

IResource::Type DebugTexture::getType()
{
    SLANG_RHI_API_FUNC;
    return baseObject->getType();
}

ITexture::Desc* DebugTexture::getDesc()
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

Result DebugTexture::setDebugName(const char* name)
{
    return baseObject->setDebugName(name);
}

const char* DebugTexture::getDebugName()
{
    return baseObject->getDebugName();
}

} // namespace rhi::debug
