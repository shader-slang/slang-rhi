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
    const SurfaceInfo& info = baseObject->getInfo();

    // format must be Format::Unknown (selecting preferred format) or any of the supported formats.
    if (config.format != Format::Unknown && !contains(info.formats, info.formatCount, config.format))
    {
        RHI_VALIDATION_ERROR("Unsupported format");
        return SLANG_E_INVALID_ARG;
    }

    // usage must be subset of supported usage.
    if (config.usage != (config.usage & info.supportedUsage))
    {
        RHI_VALIDATION_ERROR("Unsupported usage");
        return SLANG_E_INVALID_ARG;
    }

    // width and height must be greater than 0.
    if (config.width == 0 || config.height == 0)
    {
        RHI_VALIDATION_ERROR("Invalid size");
        return SLANG_E_INVALID_ARG;
    }

    // desiredImageCount must be greater than 0.
    if (config.desiredImageCount == 0)
    {
        RHI_VALIDATION_ERROR("Invalid desired image count");
        return SLANG_E_INVALID_ARG;
    }

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
