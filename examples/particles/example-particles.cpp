#include "../base/example.h"
#include "../base/frame-stats.h"
#include "../base/text-renderer.h"
#include "renderer.h"
#include "simulation-clock.h"

#include <memory>

using namespace rhi;

class ExampleParticles : public ExampleBase
{
public:
    Result init(DeviceType deviceType) override
    {
        SLANG_RETURN_ON_FAIL(
            createDevice(deviceType, {Feature::Surface, Feature::Rasterization}, {}, m_device.writeRef())
        );
        SLANG_RETURN_ON_FAIL(createWindow(m_device, "Particle vortex", 960, 720));
        SLANG_RETURN_ON_FAIL(createSurface(m_device, Format::Undefined, m_surface.writeRef()));
        SLANG_RETURN_ON_FAIL(m_device->getQueue(QueueType::Graphics, m_queue.writeRef()));
        particles::Programs programs;
        SLANG_RETURN_ON_FAIL(
            createProgram(m_device, "particles.slang", {"simulateMain"}, programs.simulate.writeRef())
        );
        SLANG_RETURN_ON_FAIL(createProgram(m_device, "particles.slang", {"spawnMain"}, programs.spawn.writeRef()));
        SLANG_RETURN_ON_FAIL(
            createProgram(m_device, "particles.slang", {"finalizeMain"}, programs.finalize.writeRef())
        );
        SLANG_RETURN_ON_FAIL(createProgram(
            m_device,
            "particles.slang",
            {"particleVertex", "particleFragment"},
            programs.particles.writeRef()
        ));
        SLANG_RETURN_ON_FAIL(
            createProgram(m_device, "particles.slang", {"logoVertex", "logoFragment"}, programs.logo.writeRef())
        );
        SLANG_RETURN_ON_FAIL(createProgram(m_device, "../raster/post.slang", {"bloomMain"}, programs.bloom.writeRef()));
        SLANG_RETURN_ON_FAIL(createProgram(
            m_device,
            "../raster/post.slang",
            {"fullscreenVertex", "toneMapFragment"},
            programs.toneMap.writeRef()
        ));
        m_renderer = std::make_unique<particles::Renderer>();
        SLANG_RETURN_ON_FAIL(m_renderer->init(m_device, m_surface->getConfig()->format, programs));
        SLANG_RETURN_ON_FAIL(m_renderer->resize(m_surface->getConfig()->width, m_surface->getConfig()->height));
        m_profiler = std::make_unique<GpuProfiler>();
        SLANG_RETURN_ON_FAIL(m_profiler->init(m_device));
        ComPtr<IShaderProgram> text;
        SLANG_RETURN_ON_FAIL(
            createProgram(m_device, "../base/text.slang", {"textVertex", "textFragment"}, text.writeRef())
        );
        m_text = std::make_unique<TextRenderer>();
        SLANG_RETURN_ON_FAIL(m_text->init(m_device, m_surface->getConfig()->format, text));

        // Live-count telemetry is independent of simulation and profiling. D3D11
        // has no RHI fence implementation, so it simply omits this optional HUD value.
        if (deviceType != DeviceType::D3D11)
        {
            FenceDesc fence = {};
            SLANG_RETURN_ON_FAIL(m_device->createFence(fence, m_readbackFence.writeRef()));
            for (auto& slot : m_readbacks)
            {
                BufferDesc desc = {};
                desc.size = sizeof(uint32_t);
                desc.usage = BufferUsage::CopyDestination;
                desc.memoryType = MemoryType::ReadBack;
                SLANG_RETURN_ON_FAIL(m_device->createBuffer(desc, nullptr, slot.buffer.writeRef()));
            }
        }
        LOG_INFO(
            "Particles: left mouse attracts; right repels; [/] emission; Space pause; N step; R reset; B bloom; D "
            "direct/indirect."
        );
        return SLANG_OK;
    }

    void shutdown() override
    {
        if (m_queue)
            m_queue->waitOnHost();
        m_text.reset();
        m_renderer.reset();
        m_profiler.reset();
        m_readbacks = {};
        m_readbackFence.setNull();
        m_surface.setNull();
        m_queue.setNull();
        m_device.setNull();
    }

    Result update(double time) override
    {
        bool visible = m_surface->getConfig() != nullptr;
        m_steps = m_clock.update(time, visible && !m_paused);
        if (visible && m_singleStep && m_paused)
            m_steps = 1;
        m_singleStep = false;
        if (visible)
            m_frameStats.update(time);
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
        SLANG_RETURN_ON_FAIL(pollReadbacks());
        SLANG_RETURN_ON_FAIL(m_profiler->beginFrame());
        auto encoder = m_queue->createCommandEncoder();
        if (m_reset)
        {
            m_renderer->reset(encoder);
            m_reset = false;
        }
        particles::SimulationParams params;
        params.spawnCount = m_emissionPerStep;
        params.mouse = m_mouseWorld;
        params.mouseForce = isMouseDown(GLFW_MOUSE_BUTTON_RIGHT)  ? -18.0f
                            : isMouseDown(GLFW_MOUSE_BUTTON_LEFT) ? 18.0f
                                                                  : 0.0f;
        for (uint32_t i = 0; i < m_steps; ++i)
            SLANG_RETURN_ON_FAIL(m_renderer->step(encoder, params, m_profiler.get()));
        SLANG_RETURN_ON_FAIL(m_renderer->render(encoder, image, m_bloom, m_profiler.get()));
        SLANG_RETURN_ON_FAIL(drawOverlay(encoder, image));

        Readback* readback = nullptr;
        if (m_readbackFence && m_frameNumber++ % 30 == 0)
        {
            for (auto& slot : m_readbacks)
                if (!slot.value)
                {
                    readback = &slot;
                    encoder->copyBuffer(slot.buffer, 0, m_renderer->countBuffer(), 0, sizeof(uint32_t));
                    break;
                }
        }
        SLANG_RETURN_ON_FAIL(m_profiler->endFrame());
        ComPtr<ICommandBuffer> commands;
        SLANG_RETURN_ON_FAIL(encoder->finish(commands.writeRef()));
        ICommandBuffer* commandPointer = commands;
        IFence* fencePointer = m_readbackFence;
        SubmitDesc submit = {};
        submit.commandBuffers = &commandPointer;
        submit.commandBufferCount = 1;
        if (readback)
        {
            readback->value = ++m_fenceValue;
            readback->generation = m_generation;
            submit.signalFences = &fencePointer;
            submit.signalFenceValues = &readback->value;
            submit.signalFenceCount = 1;
        }
        SLANG_RETURN_ON_FAIL(m_queue->submit(submit));
        return m_surface->present();
    }

    void onResize(int width, int height, int framebufferWidth, int framebufferHeight) override
    {
        if (!m_queue || !m_renderer)
            return;
        m_frameStats.reset();
        m_clock.reset();
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

    void onMousePosition(float x, float y) override
    {
        // Mirrored coordinates originate in the main window. Normalize against
        // that window so different DPI scales cannot change the interaction.
        auto source = detail::mainExample ? detail::mainExample->m_window : m_window;
        int width, height;
        glfwGetWindowSize(source, &width, &height);
        if (width > 0 && height > 0)
            m_mouseWorld = {(2 * x / width - 1) * 3.6f * float(width) / height, (1 - 2 * y / height) * 3.6f};
    }

    void onKey(int key, int scancode, int action, int mods) override
    {
        if (action != GLFW_PRESS && action != GLFW_REPEAT)
            return;
        if (key == GLFW_KEY_LEFT_BRACKET)
            m_emissionPerStep = m_emissionPerStep > 100 ? m_emissionPerStep - 100 : 0;
        if (key == GLFW_KEY_RIGHT_BRACKET)
            m_emissionPerStep = std::min(4000u, m_emissionPerStep + 100);
        if (action != GLFW_PRESS)
            return;
        if (key == GLFW_KEY_SPACE)
            m_paused = !m_paused;
        if (key == GLFW_KEY_N)
            m_singleStep = true;
        if (key == GLFW_KEY_B)
            m_bloom = !m_bloom;
        if (key == GLFW_KEY_D)
            m_renderer->setIndirect(!m_renderer->indirect());
        if (key == GLFW_KEY_R)
        {
            m_reset = true;
            m_clock.reset();
            m_countValid = false;
            ++m_generation;
        }
    }

private:
    struct Readback
    {
        ComPtr<IBuffer> buffer;
        uint64_t value = 0, generation = 0;
    };

    Result pollReadbacks()
    {
        if (!m_readbackFence)
            return SLANG_OK;
        uint64_t completed;
        SLANG_RETURN_ON_FAIL(m_readbackFence->getCurrentValue(&completed));
        for (auto& slot : m_readbacks)
        {
            if (!slot.value || slot.value > completed)
                continue;
            if (slot.generation == m_generation && slot.value > m_lastReadback)
            {
                void* data;
                SLANG_RETURN_ON_FAIL(m_device->mapBuffer(slot.buffer, CpuAccessMode::Read, &data));
                m_liveCount = *static_cast<const uint32_t*>(data);
                SLANG_RETURN_ON_FAIL(m_device->unmapBuffer(slot.buffer));
                m_countValid = true;
                m_lastReadback = slot.value;
            }
            slot.value = 0;
        }
        return SLANG_OK;
    }

    Result drawOverlay(ICommandEncoder* encoder, ITexture* image)
    {
        char text[1024], count[64];
        if (m_countValid)
            snprintf(count, sizeof(count), "%u / %u (DELAYED)", m_liveCount, m_renderer->capacity());
        else
            snprintf(count, sizeof(count), "%s", m_readbackFence ? "PENDING" : "READBACK UNAVAILABLE");
        double simulation = 0, rendering = 0, post = 0;
        for (const auto& sample : m_profiler->samples())
        {
            if (sample.name == "Simulation step")
                simulation += sample.milliseconds;
            else if (sample.name == "Particles + logo")
                rendering += sample.milliseconds;
            else
                post += sample.milliseconds;
        }
        char timing[160];
        if (!m_profiler->supported())
            snprintf(timing, sizeof(timing), "GPU TIMING UNAVAILABLE");
        else if (m_profiler->samples().empty())
            snprintf(timing, sizeof(timing), "GPU TIMING PENDING");
        else
            snprintf(timing, sizeof(timing), "GPU MS: SIM %.2f / DRAW %.2f / POST %.2f", simulation, rendering, post);
        snprintf(
            text,
            sizeof(text),
            "PARTICLE VORTEX / %s\n%.1f FPS / %.2f MS APP LOOP\n%s\nLIVE %s\n%s / EMIT %u PER SEC / %s\n\n"
            "LEFT: ATTRACT / RIGHT: REPEL\n[ / ] EMISSION / SPACE PAUSE / N STEP\nR RESET / B BLOOM / D DRAW MODE",
            getRHI()->getDeviceTypeName(m_deviceType),
            m_frameStats.fps(),
            m_frameStats.milliseconds(),
            timing,
            count,
            m_renderer->indirect() ? "INDIRECT" : "DIRECT CAPACITY",
            m_emissionPerStep * 120,
            m_paused ? "PAUSED" : "RUNNING"
        );
        m_text->clear();
        SLANG_RETURN_ON_FAIL(m_text->addRect({10, 10}, {540, 155}, {0.003f, 0.006f, 0.012f, 0.9f}));
        SLANG_RETURN_ON_FAIL(m_text->addText(text, {16, 16}, 1.5f, {0.75f, 0.85f, 1.0f, 1.0f}));
        return m_text->render(encoder, image);
    }

    ComPtr<IDevice> m_device;
    ComPtr<ISurface> m_surface;
    ComPtr<ICommandQueue> m_queue;
    std::unique_ptr<particles::Renderer> m_renderer;
    std::unique_ptr<TextRenderer> m_text;
    std::unique_ptr<GpuProfiler> m_profiler;
    ComPtr<IFence> m_readbackFence;
    std::array<Readback, 3> m_readbacks;
    uint64_t m_fenceValue = 0, m_lastReadback = 0, m_generation = 0, m_frameNumber = 0;
    particles::SimulationClock m_clock;
    FrameStats m_frameStats;
    math::float2 m_mouseWorld = {};
    uint32_t m_steps = 0, m_emissionPerStep = 400, m_liveCount = 0;
    bool m_paused = false, m_singleStep = false, m_reset = true, m_bloom = true, m_countValid = false;
    Result m_resizeResult = SLANG_OK;
};

EXAMPLE_MAIN(ExampleParticles)
