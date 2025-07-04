#include "debug-surface.h"
#include "debug-helper-functions.h"

namespace rhi::debug {

const SurfaceInfo& DebugSurface::getInfo()
{
    SLANG_RHI_API_FUNC;

    return baseObject->getInfo();
}

const SurfaceConfig* DebugSurface::getConfig()
{
    SLANG_RHI_API_FUNC;

    return baseObject->getConfig();
}

Result DebugSurface::configure(const SurfaceConfig& config)
{
    const SurfaceInfo& info = baseObject->getInfo();

    m_configured = false;

    // format must be Format::Undefined (selecting preferred format) or any of the supported formats.
    if (config.format != Format::Undefined && !contains(info.formats, info.formatCount, config.format))
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

    Result result = baseObject->configure(config);

    if (SLANG_SUCCEEDED(result))
    {
        m_configured = true;
        m_state = State::Initial;
    }

    return result;
}

Result DebugSurface::unconfigure()
{
    if (!m_configured)
    {
        RHI_VALIDATION_WARNING("Surface is not configured.");
    }

    Result result = baseObject->unconfigure();

    if (SLANG_SUCCEEDED(result))
    {
        m_configured = false;
    }

    return result;
}

Result DebugSurface::acquireNextImage(ITexture** outTexture)
{
    SLANG_RHI_API_FUNC;

    if (!m_configured)
    {
        RHI_VALIDATION_ERROR("Surface is not configured.");
        return SLANG_FAIL;
    }

    if (m_state == State::ImageAcquired)
    {
        RHI_VALIDATION_ERROR("Image already aquired. Image needs to be presented before acquiring a new one.");
        return SLANG_FAIL;
    }

    Result result = baseObject->acquireNextImage(outTexture);

    if (SLANG_SUCCEEDED(result))
    {
        m_state = State::ImageAcquired;
    }

    return result;
}

Result DebugSurface::present()
{
    SLANG_RHI_API_FUNC;

    if (!m_configured)
    {
        RHI_VALIDATION_ERROR("Surface is not configured.");
        return SLANG_FAIL;
    }

    if (m_state != State::ImageAcquired)
    {
        RHI_VALIDATION_ERROR("No image acquired to present.");
        return SLANG_FAIL;
    }

    Result result = baseObject->present();

    if (SLANG_SUCCEEDED(result))
    {
        m_state = State::ImagePresented;
    }

    return result;
}

} // namespace rhi::debug
