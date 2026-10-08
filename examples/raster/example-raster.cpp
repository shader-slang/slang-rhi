#include "../base/example.h"
#include "../base/frame-stats.h"
#include "../base/text-renderer.h"
#include "renderer.h"

#include <memory>

using namespace rhi;

class ExampleRaster : public ExampleBase
{
public:
    Result init(DeviceType deviceType) override
    {
        SLANG_RETURN_ON_FAIL(
            createDevice(deviceType, {Feature::Surface, Feature::Rasterization}, {}, m_device.writeRef())
        );
        SLANG_RETURN_ON_FAIL(createWindow(m_device, "Slang logo", 960, 640));
        SLANG_RETURN_ON_FAIL(createSurface(m_device, Format::Undefined, m_surface.writeRef()));
        SLANG_RETURN_ON_FAIL(m_device->getQueue(QueueType::Graphics, m_queue.writeRef()));

        raster::Programs programs;
        SLANG_RETURN_ON_FAIL(
            createProgram(m_device, "raster.slang", {"shadowVertex", "shadowFragment"}, programs.shadow.writeRef())
        );
        SLANG_RETURN_ON_FAIL(
            createProgram(m_device, "raster.slang", {"sceneVertex", "sceneFragment"}, programs.scene.writeRef())
        );
        SLANG_RETURN_ON_FAIL(
            createProgram(m_device, "raster.slang", {"sphereVertex", "sceneFragment"}, programs.spheres.writeRef())
        );
        SLANG_RETURN_ON_FAIL(createProgram(
            m_device,
            "raster.slang",
            {"sphereShadowVertex", "shadowFragment"},
            programs.sphereShadow.writeRef()
        ));
        SLANG_RETURN_ON_FAIL(createProgram(m_device, "post.slang", {"bloomMain"}, programs.bloom.writeRef()));
        SLANG_RETURN_ON_FAIL(
            createProgram(m_device, "post.slang", {"fullscreenVertex", "toneMapFragment"}, programs.toneMap.writeRef())
        );
        SLANG_RETURN_ON_FAIL(createProgram(
            m_device,
            "raster.slang",
            {"backgroundVertex", "backgroundFragment"},
            programs.background.writeRef()
        ));
        SLANG_RETURN_ON_FAIL(
            createProgram(m_device, "environment.slang", {"environmentMain"}, programs.environment.writeRef())
        );
        SLANG_RETURN_ON_FAIL(
            createProgram(m_device, "environment.slang", {"brdfMain"}, programs.environmentBrdf.writeRef())
        );
        m_renderer = std::make_unique<raster::Renderer>();
        SLANG_RETURN_ON_FAIL(m_renderer->init(m_device, m_surface->getConfig()->format, programs));
        SLANG_RETURN_ON_FAIL(m_renderer->resize(m_surface->getConfig()->width, m_surface->getConfig()->height));
        ComPtr<IShaderProgram> textProgram;
        SLANG_RETURN_ON_FAIL(
            createProgram(m_device, "../base/text.slang", {"textVertex", "textFragment"}, textProgram.writeRef())
        );
        m_text = std::make_unique<TextRenderer>();
        SLANG_RETURN_ON_FAIL(m_text->init(m_device, m_surface->getConfig()->format, textProgram));
        m_profiler = std::make_unique<GpuProfiler>();
        SLANG_RETURN_ON_FAIL(m_profiler->init(m_device));
        LOG_INFO(
            "Logo controls: left drag orbit; right drag light; wheel zoom; [/] roughness; -/= exposure; B bloom; R "
            "reset; E environment; ,/. rotate environment; I environment lighting; M MSAA; S spheres."
        );
        updateTitle();
        return SLANG_OK;
    }

    void shutdown() override
    {
        if (m_queue)
            m_queue->waitOnHost();
        m_text.reset();
        m_profiler.reset();
        m_renderer.reset();
        m_surface.setNull();
        m_queue.setNull();
        m_device.setNull();
    }

    Result update(double time) override
    {
        if (m_surface->getConfig() && m_frameStats.update(time))
            updateTitle();
        return m_resizeResult;
    }

    Result draw() override
    {
        if (!m_surface->getConfig())
            return SLANG_OK;
        ComPtr<ITexture> image;
        SLANG_RETURN_ON_FAIL(m_surface->acquireNextImage(image.writeRef()));
        if (!image)
            return SLANG_OK;
        auto encoder = m_queue->createCommandEncoder();
        SLANG_RETURN_ON_FAIL(m_profiler->beginFrame());
        SLANG_RETURN_ON_FAIL(m_renderer->render(encoder, image, m_settings, m_profiler.get()));
        SLANG_RETURN_ON_FAIL(drawOverlay(encoder, image));
        SLANG_RETURN_ON_FAIL(m_profiler->endFrame());
        ComPtr<ICommandBuffer> commands;
        SLANG_RETURN_ON_FAIL(encoder->finish(commands.writeRef()));
        SLANG_RETURN_ON_FAIL(m_queue->submit(commands));
        return m_surface->present();
    }

    void onResize(int width, int height, int framebufferWidth, int framebufferHeight) override
    {
        if (!m_queue || !m_renderer)
            return;
        m_frameStats.reset();
        m_resizeResult = m_queue->waitOnHost();
        if (SLANG_FAILED(m_resizeResult))
            return;
        if (framebufferWidth <= 0 || framebufferHeight <= 0)
        {
            m_resizeResult = m_surface->unconfigure();
            return;
        }
        SurfaceConfig config = {};
        config.format = m_surface->getInfo().preferredFormat;
        config.width = framebufferWidth;
        config.height = framebufferHeight;
        m_resizeResult = m_surface->configure(config);
        if (SLANG_SUCCEEDED(m_resizeResult))
            m_resizeResult = m_renderer->resize(framebufferWidth, framebufferHeight);
    }

    void onMouseButton(int button, int action, int mods) override { m_lastMouse = {getMouseX(), getMouseY()}; }

    void onMousePosition(float x, float y) override
    {
        // The framework mirrors absolute positions to every backend window.
        // Use a fixed sensitivity so those shared events produce identical views.
        math::float2 delta = math::float2(x, y) - m_lastMouse;
        m_lastMouse = {x, y};
        if (isMouseDown(GLFW_MOUSE_BUTTON_LEFT))
        {
            m_settings.yaw -= delta.x * 0.006f;
            m_settings.pitch = std::clamp(m_settings.pitch + delta.y * 0.006f, 0.03f, 1.3f);
        }
        if (isMouseDown(GLFW_MOUSE_BUTTON_RIGHT))
        {
            m_settings.lightYaw -= delta.x * 0.006f;
            m_settings.lightPitch = std::clamp(m_settings.lightPitch + delta.y * 0.006f, 0.15f, 1.45f);
        }
    }

    void onScroll(float x, float y) override
    {
        m_settings.distance = std::clamp(m_settings.distance * std::exp(-0.1f * y), 4.0f, 20.0f);
    }

    void onKey(int key, int scancode, int action, int mods) override
    {
        if (action != GLFW_PRESS && action != GLFW_REPEAT)
            return;
        if (key == GLFW_KEY_B && action == GLFW_PRESS)
            m_settings.bloom = !m_settings.bloom;
        if (key == GLFW_KEY_E && action == GLFW_PRESS)
            m_settings.environmentPreset = 1 - m_settings.environmentPreset;
        if (key == GLFW_KEY_I && action == GLFW_PRESS)
            m_settings.environmentLighting = !m_settings.environmentLighting;
        if (key == GLFW_KEY_M && action == GLFW_PRESS && m_renderer->supportsMsaa())
            m_settings.msaa = !m_settings.msaa;
        if (key == GLFW_KEY_S && action == GLFW_PRESS)
            m_settings.spheres = !m_settings.spheres;
        if (key == GLFW_KEY_COMMA || key == GLFW_KEY_PERIOD)
            m_settings.environmentRotation =
                std::remainder(m_settings.environmentRotation + (key == GLFW_KEY_PERIOD ? 0.1f : -0.1f), 6.2831853f);
        if (key == GLFW_KEY_R)
            m_settings = {};
        if (key == GLFW_KEY_LEFT_BRACKET || key == GLFW_KEY_RIGHT_BRACKET)
            m_settings.roughness =
                std::clamp(m_settings.roughness + (key == GLFW_KEY_RIGHT_BRACKET ? 0.05f : -0.05f), 0.08f, 1.0f);
        if (key == GLFW_KEY_MINUS || key == GLFW_KEY_EQUAL)
            m_settings.exposure =
                std::clamp(m_settings.exposure + (key == GLFW_KEY_EQUAL ? 0.25f : -0.25f), -4.0f, 4.0f);
        updateTitle();
    }

private:
    const char* msaaStatus() const
    {
        return !m_renderer->supportsMsaa() ? "UNAVAILABLE" : m_settings.msaa ? "4X" : "OFF";
    }

    Result drawOverlay(ICommandEncoder* encoder, ITexture* image)
    {
        char text[768];
        snprintf(
            text,
            sizeof(text),
            "SLANG LOGO / %s\n"
            "%.1f FPS / %.2f MS - APP LOOP\n\n"
            "LOGO ROUGH %.2f   [ / ]\n"
            "EXPOSURE   %+.2f   - / =\n"
            "BLOOM      %s   B\n"
            "MSAA       %s   M\n"
            "SPHERES    %u   S\n"
            "ENV        %s   E\n"
            "ENV ANGLE  %+.0f DEG   , / .\n"
            "ENV LIGHT  %s   I\n\n"
            "LEFT DRAG: ORBIT\n"
            "RIGHT DRAG: LIGHT\n"
            "WHEEL: ZOOM / R: RESET",
            getRHI()->getDeviceTypeName(m_deviceType),
            m_frameStats.fps(),
            m_frameStats.milliseconds(),
            m_settings.roughness,
            m_settings.exposure,
            m_settings.bloom ? "ON " : "OFF",
            msaaStatus(),
            m_settings.spheres ? raster::kSphereCount : 0u,
            m_settings.environmentPreset == 0 ? "STUDIO " : "OUTDOOR",
            m_settings.environmentRotation * (180.0f / 3.14159265f),
            m_settings.environmentLighting ? "ON " : "OFF"
        );
        int windowWidth, windowHeight;
        glfwGetWindowSize(m_window, &windowWidth, &windowHeight);
        float pixelRatio = float(image->getDesc().size.width) / std::max(windowWidth, 1);
        float scale = std::max(1.0f, std::round(2.0f * pixelRatio));
        math::float2 origin(12 * pixelRatio, 12 * pixelRatio);
        m_text->clear();
        SLANG_RETURN_ON_FAIL(m_text->addText(text, origin + math::float2(scale), scale, {0.005f, 0.007f, 0.01f, 1}));
        SLANG_RETURN_ON_FAIL(m_text->addText(text, origin, scale, {0.82f, 0.88f, 0.96f, 1}));
        char timing[96];
        if (!m_profiler->supported())
            snprintf(timing, sizeof(timing), "GPU TIMING UNAVAILABLE");
        else if (m_profiler->samples().empty())
            snprintf(timing, sizeof(timing), "GPU TIMING PENDING");
        else
            snprintf(timing, sizeof(timing), "GPU RENDER %.2f MS", m_profiler->samples()[0].milliseconds);
        SLANG_RETURN_ON_FAIL(
            m_text->addText(timing, {origin.x, float(image->getDesc().size.height) - 12 * scale}, scale)
        );
        return m_text->render(encoder, image);
    }

    void updateTitle()
    {
        char title[512];
        snprintf(
            title,
            sizeof(title),
            "Slang logo | %s | %.1f FPS / %.2f ms | roughness %.2f | exposure %+.2f | bloom %s | MSAA %s | %s | IBL %s "
            "| spheres %u",
            getRHI()->getDeviceTypeName(m_deviceType),
            m_frameStats.fps(),
            m_frameStats.milliseconds(),
            m_settings.roughness,
            m_settings.exposure,
            m_settings.bloom ? "on" : "off",
            msaaStatus(),
            m_settings.environmentPreset == 0 ? "studio" : "outdoor",
            m_settings.environmentLighting ? "on" : "off",
            m_settings.spheres ? raster::kSphereCount : 0u
        );
        glfwSetWindowTitle(m_window, title);
    }

    ComPtr<IDevice> m_device;
    ComPtr<ISurface> m_surface;
    ComPtr<ICommandQueue> m_queue;
    std::unique_ptr<raster::Renderer> m_renderer;
    std::unique_ptr<TextRenderer> m_text;
    std::unique_ptr<GpuProfiler> m_profiler;
    FrameStats m_frameStats;
    raster::Settings m_settings;
    math::float2 m_lastMouse;
    Result m_resizeResult = SLANG_OK;
};

EXAMPLE_MAIN(ExampleRaster)
