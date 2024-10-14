#include "debug-surface.h"
#include "debug-helper-functions.h"

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
    return baseObject->getCurrentTexture(outTexture);
}

Result DebugSurface::present()
{
    return baseObject->present();
}

} // namespace rhi::debug
