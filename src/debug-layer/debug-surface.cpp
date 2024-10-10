#include "debug-surface.h"
#include "debug-helper-functions.h"
#include "debug-texture.h"

namespace rhi::debug {

const SurfaceInfo& DebugSurface::getInfo()
{
    return baseObject->getInfo();
}

const SurfaceConfig& DebugSurface::getConfig()
{
    return baseObject->getConfig();
}

Result DebugSurface::configure(const SurfaceConfig& config)
{
    return baseObject->configure(config);
}

Result DebugSurface::getCurrentTexture(ITexture** outTexture)
{
    RefPtr<DebugTexture> texture = new DebugTexture(ctx);
    SLANG_RETURN_ON_FAIL(baseObject->getCurrentTexture(texture->baseObject.writeRef()));
    returnComPtr(outTexture, texture);
    return SLANG_OK;
}

Result DebugSurface::present()
{
    return baseObject->present();
}

} // namespace rhi::debug
