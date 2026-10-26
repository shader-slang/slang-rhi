#include "example.h"
#include "example-runner.h"

#include <cstdarg>
#include <cstdio>

namespace rhi {

ExampleBase::ExampleBase() = default;

ExampleBase::~ExampleBase()
{
    m_textRenderer.reset();
    m_presentationSurface.setNull();
    m_presentationQueue.setNull();
    destroyWindow();
}

Result ExampleBase::createDevice(
    DeviceType deviceType,
    std::vector<Feature> requiredFeatures,
    std::vector<std::pair<std::string, std::string>> preprocessorMacros,
    IDevice** outDevice
)
{
    SLANG_RETURN_ON_FAIL(
        rhi::createDevice(
            deviceType,
            requiredFeatures,
            m_desc->shaderSearchPath,
            preprocessorMacros,
            outDevice,
            &m_missingFeatures
        )
    );
    m_deviceType = (*outDevice)->getDeviceType();
    return SLANG_OK;
}

Result ExampleBase::createWindow(IDevice* device)
{
    const auto& deviceInfo = device->getInfo();
    char fullTitle[1024];
    snprintf(
        fullTitle,
        sizeof(fullTitle),
        "%s | %s (%s)",
        m_desc->title,
        getRHI()->getDeviceTypeName(deviceInfo.deviceType),
        deviceInfo.adapterName
    );
    m_windowTitle = fullTitle;

    Result result = m_runner->app.runOnMainThread(
        [&]() -> Result
        {
            m_window = m_runner->app.createWindow(*m_runner, fullTitle, deviceInfo.adapterName);
            if (!m_window)
                return SLANG_FAIL;
            glfwGetWindowSize(m_window, &m_windowWidth, &m_windowHeight);
            glfwGetFramebufferSize(m_window, &m_framebufferWidth, &m_framebufferHeight);
            // Emscripten's GLFW writes both outputs, even when only the X scale is used.
            float contentScaleY;
            glfwGetWindowContentScale(m_window, &m_contentScale, &contentScaleY);
            return SLANG_OK;
        }
    );
    SLANG_RETURN_ON_FAIL(result);
    return updateWindowTitle();
}

Result ExampleBase::updateWindowTitle()
{
    return m_runner->app.runOnMainThread(
        [&]() -> Result
        {
            if (!m_window)
                return SLANG_E_INVALID_HANDLE;
            m_runner->app.updateWindowTitle(*m_runner, m_windowTitle.c_str(), m_frameStats.fps(), m_vsync);
            return SLANG_OK;
        }
    );
}

void ExampleBase::destroyWindow()
{
    if (!m_window)
        return;
    m_runner->app.runOnMainThread(
        [&]() -> Result
        {
            m_runner->app.destroyWindow(*m_runner);
            m_window = nullptr;
            return SLANG_OK;
        }
    );
}

bool ExampleBase::shouldClose() const
{
    return m_runner && m_runner->stopping;
}

Result ExampleBase::createSurface(IDevice* device, Format format, ISurface** outSurface)
{
    ASSERT(m_window, "Window must be created before creating surface");
    SLANG_RETURN_ON_FAIL(device->getQueue(QueueType::Graphics, m_presentationQueue.writeRef()));
    // Native surface creation may modify the Cocoa view. Transfer exclusive device access
    // to the main thread for this operation; subsequent rendering stays on the worker.
    Result result = m_runner->app.runOnMainThread(
        [&]() -> Result
        {
            SLANG_RHI_DEVICE_SCOPE(device);
            glfwGetWindowSize(m_window, &m_windowWidth, &m_windowHeight);
            glfwGetFramebufferSize(m_window, &m_framebufferWidth, &m_framebufferHeight);
            SLANG_RETURN_ON_FAIL(device->createSurface(m_runner->app.getWindowHandle(*m_runner), outSurface));
            SurfaceConfig surfaceConfig;
            surfaceConfig.width = m_framebufferWidth;
            surfaceConfig.height = m_framebufferHeight;
            surfaceConfig.format = format;
            return configureSurface(*outSurface, surfaceConfig);
        }
    );
    SLANG_RETURN_ON_FAIL(result);
    m_presentationSurface = *outSurface;
    return SLANG_OK;
}

Result ExampleBase::configureSurface(ISurface* surface, SurfaceConfig config)
{
    config.vsync = m_vsync;
    return surface->configure(config);
}

Result ExampleBase::applyVsync()
{
    if (!m_vsyncChanged)
        return SLANG_OK;
    if (m_presentationSurface && m_presentationSurface->getConfig())
    {
        SurfaceConfig config = *m_presentationSurface->getConfig();
        if (config.vsync != m_vsync)
        {
            SLANG_RETURN_ON_FAIL(m_presentationQueue->waitOnHost());
            SLANG_RETURN_ON_FAIL(configureSurface(m_presentationSurface, config));
        }
    }
    // An unconfigured (minimized) surface picks up the preference on restore.
    m_vsyncChanged = false;
    m_frameStats.reset();
    return updateWindowTitle();
}

Result ExampleBase::presentSurface()
{
    SLANG_RETURN_ON_FAIL(m_presentationSurface->present());
    if (m_frameStats.update(glfwGetTime()))
        return updateWindowTitle();
    return SLANG_OK;
}

Result ExampleBase::initProfiler(IDevice* device)
{
    auto profiler = std::make_unique<Profiler>();
    SLANG_RETURN_ON_FAIL(profiler->init(device));
    m_profiler = std::move(profiler);
    return SLANG_OK;
}

Profiler& ExampleBase::getProfiler()
{
    ASSERT(m_profiler, "Profiler must be initialized before accessing it");
    return *m_profiler;
}

void ExampleBase::toggleProfiler()
{
    if (!m_profiler)
        return;
    m_profiler->setEnabled(!m_profiler->enabled());
}

Result ExampleBase::initOverlay(IDevice* device, Format format)
{
    m_textRenderer = std::make_unique<TextRenderer>();
    return m_textRenderer->init(device, format);
}

void ExampleBase::clearOverlay()
{
    if (m_textRenderer)
        m_textRenderer->clear();
}

void ExampleBase::printOverlayHeader()
{
    bool first = true;
    for (const auto& shortcut : detail::kExampleShortcuts)
    {
        if (shortcut.key == GLFW_KEY_F4 && !m_profiler)
            continue;
        if (shortcut.key == GLFW_KEY_F5 && (!m_runner || !m_runner->app.hasExamplePicker()))
            continue;
        printOverlay("%s^3%s^0: %s", first ? "" : " | ", shortcut.keyName, shortcut.label);
        first = false;
    }
    printOverlay("\n");
}

void ExampleBase::printCameraControls()
{
    printOverlay("^3Left drag^0: Look | ^3W/A/S/D^0: Move | ^3Q/E^0: Down/up | ^3Shift^0: Faster\n");
}

void ExampleBase::printOverlay(const char* format, ...)
{
    if (!format || !m_overlayVisible || !m_examplePickerText.empty() || !m_textRenderer)
        return;
    va_list args;
    va_start(args, format);
    va_list sizeArgs;
    va_copy(sizeArgs, args);
    int size = std::vsnprintf(nullptr, 0, format, sizeArgs);
    va_end(sizeArgs);
    if (size < 0)
    {
        va_end(args);
        return;
    }
    std::string text(size_t(size) + 1, '\0');
    int written = std::vsnprintf(text.data(), text.size(), format, args);
    va_end(args);
    if (written != size)
        return;
    m_textRenderer->append(std::string_view(text.data(), size_t(size)));
}

Result ExampleBase::drawOverlay(ICommandEncoder* encoder, ITexture* image)
{
    if ((!m_overlayVisible && m_examplePickerText.empty()) || !m_textRenderer)
        return SLANG_OK;
    if (!encoder || !image)
        return SLANG_E_INVALID_ARG;
    // Content scale already maps UI dimensions to framebuffer pixels, including on Retina displays.
    if (!m_examplePickerText.empty())
    {
        m_textRenderer->clear();
        m_textRenderer->append(m_examplePickerText);
    }
    else if (m_profiler)
        printOverlay("%s", m_profiler->getDisplayText().c_str());
    return m_textRenderer->render(encoder, image, m_contentScale);
}

} // namespace rhi
