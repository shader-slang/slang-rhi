#include "debug-texture.h"
#include "debug-helper-functions.h"

namespace rhi::debug {

IResource::Type DebugTextureResource::getType()
{
    SLANG_RHI_API_FUNC;
    return baseObject->getType();
}

ITextureResource::Desc* DebugTextureResource::getDesc()
{
    SLANG_RHI_API_FUNC;
    return baseObject->getDesc();
}

Result DebugTextureResource::getNativeResourceHandle(InteropHandle* outHandle)
{
    SLANG_RHI_API_FUNC;
    return baseObject->getNativeResourceHandle(outHandle);
}

Result DebugTextureResource::getSharedHandle(InteropHandle* outHandle)
{
    SLANG_RHI_API_FUNC;
    return baseObject->getSharedHandle(outHandle);
}

Result DebugTextureResource::setDebugName(const char* name)
{
    return baseObject->setDebugName(name);
}

const char* DebugTextureResource::getDebugName()
{
    return baseObject->getDebugName();
}

} // namespace rhi::debug
