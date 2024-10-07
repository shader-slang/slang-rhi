#include "debug-surface.h"
#include "debug-helper-functions.h"
#include "debug-texture.h"

namespace rhi::debug {

const SurfaceInfo& DebugSurface::getInfo()
{
    SLANG_RHI_API_FUNC;

    return baseObject->getInfo();
}

const SurfaceConfig& DebugSurface::getConfig()
{
    SLANG_RHI_API_FUNC;

    return baseObject->getConfig();
}

Result DebugSurface::configure(const SurfaceConfig& config)
{
    SLANG_RHI_API_FUNC;

    m_configured = false;

    if (config.format == Format::Unknown)
    {
        RHI_VALIDATION_INFO("Configuring with unknown surface format, choosing the preferred format.");
    }
    if (config.width == 0 || config.height == 0)
    {
        RHI_VALIDATION_ERROR("Configuring with zero width or height.");
        return SLANG_FAIL;
    }

    Result result = baseObject->configure(config);
    m_configured = SLANG_SUCCEEDED(result);
    return result;
}

Result DebugSurface::getCurrentTexture(ITexture** outTexture)
{
    SLANG_RHI_API_FUNC;

    if (!m_configured)
    {
        RHI_VALIDATION_ERROR("Surface is not configured.");
        return SLANG_FAIL;
    }

    RefPtr<DebugTexture> texture = new DebugTexture();
    SLANG_RETURN_ON_FAIL(baseObject->getCurrentTexture(texture->baseObject.writeRef()));
    returnComPtr(outTexture, texture);
    return SLANG_OK;
}

Result DebugSurface::present()
{
    SLANG_RHI_API_FUNC;

    if (!m_configured)
    {
        RHI_VALIDATION_ERROR("Surface is not configured.");
        return SLANG_FAIL;
    }

    return baseObject->present();
}

} // namespace rhi::debug
