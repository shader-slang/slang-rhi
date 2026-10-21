#pragma once

#include "utils.h"
#include "text-renderer.h"
#include "profiler.h"

#include <slang.h>
#include <slang-rhi.h>

#include <GLFW/glfw3.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string_view>
#include <string>
#include <thread>
#include <vector>

#if SLANG_WINDOWS_FAMILY
#define GLFW_EXPOSE_NATIVE_WIN32
#elif SLANG_LINUX_FAMILY
#define GLFW_EXPOSE_NATIVE_X11
#elif SLANG_APPLE_FAMILY
#define GLFW_EXPOSE_NATIVE_COCOA
#endif
#include <GLFW/glfw3native.h>
#include <slang-rhi/glfw.h>

// X11 defines None as a macro after slang-rhi.h has already removed it.
#ifdef None
#undef None
#endif

namespace rhi {

struct ExampleDesc;

namespace detail {
struct ExampleRunner;
inline constexpr int kInitialWindowWidth = 960;
inline constexpr int kInitialWindowHeight = 540;
} // namespace detail

// Base class for examples.
// Derive from this class to implement an example.
// The runner calls all lifecycle methods and input callbacks on one dedicated thread per example.
// Use the window helpers below; GLFW window operations belong to the main thread.
class ExampleBase
{
public:
    // Constructs an example without creating device, window, or overlay resources.
    ExampleBase();
    // Destroys the window and releases the overlay; call shutdown() first.
    virtual ~ExampleBase();

    // Called to initialize the example for the specified device type.
    virtual Result init(DeviceType deviceType) = 0;
    // Called to shut down the example, including after a partially failed init().
    virtual void shutdown() = 0;
    // Called every frame to update the example. All examples receive the same time unless --free-running is used.
    virtual Result update(double time) = 0;
    // Called every frame to render the example.
    virtual Result draw() = 0;

    // Event callbacks

    // Called when the window is resized.
    virtual Result onResize(int width, int height, int framebufferWidth, int framebufferHeight) { return SLANG_OK; }
    // Called when the mouse is moved.
    virtual void onMousePosition(float x, float y) {}
    // Called when the mouse enters or leaves any backend window.
    virtual void onMouseEnter(bool entered) {}
    // Called when a mouse button is pressed or released.
    virtual void onMouseButton(int button, int action, int mods) {}
    // Called when the mouse wheel is scrolled.
    virtual void onScroll(float x, float y) {}
    // Called when a key is pressed, released, or repeated.
    virtual void onKey(int key, int scancode, int action, int mods) {}

    // Accessors for mouse state

    // Returns true if the specified mouse button is currently down.
    bool isMouseDown(int button) const
    {
        return (button >= 0 && button < SLANG_COUNT_OF(m_mouseDown)) ? m_mouseDown[button] : false;
    }

    // Returns the current mouse X position.
    float getMouseX() const { return m_mousePos[0]; }
    // Returns the current mouse Y position.
    float getMouseY() const { return m_mousePos[1]; }

    // Device management

    // Create a device and retain the missing features if the example requires
    // features that this backend does not support.
    Result createDevice(
        DeviceType deviceType,
        std::vector<Feature> requiredFeatures,
        std::vector<std::pair<std::string, std::string>> preprocessorMacros,
        IDevice** outDevice
    );

    // Window management

    // Creates a window using the registered title and the common initial size.
    // The initial layout reduces the size as needed to fit all backend windows.
    // Automatically appends device, adapter, FPS, and VSync information to the title.
    Result createWindow(IDevice* device);
    // Destroys the window.
    void destroyWindow();
    // Returns whether the window has been asked to close.
    bool shouldClose() const;
    // Returns the cached window size in the same coordinates as mouse input.
    int getWindowWidth() const { return m_windowWidth; }
    int getWindowHeight() const { return m_windowHeight; }

    // Surface management

    // Creates a surface for the window with the specified format.
    // Use Format::Undefined to use the preferred format.
    Result createSurface(IDevice* device, Format format, ISurface** outSurface);
    // Configures a surface using the shared VSync preference, including after resizing.
    Result configureSurface(ISurface* surface, SurfaceConfig config);
    // Presents the window surface and records application frame timing.
    Result presentSurface();
    // Applied by the runner between frames; input callbacks run on the owning worker.
    void toggleVsync()
    {
        m_vsync = !m_vsync;
        m_vsyncChanged = true;
    }

    // Returns the current frame statistics.
    const FrameStats& getFrameStats() const { return m_frameStats; }

    // Profiler

    // Initializes optional profiling of explicitly marked regions on the example's queue.
    // Initialize once; shutdown() must wait for submissions before destruction.
    Result initProfiler(IDevice* device);

    // Returns the profiler. Requires successful initProfiler().
    Profiler& getProfiler();

    // Toggles profiling independently of overlay visibility. Does nothing if uninitialized.
    void toggleProfiler();

    // Overlay utility

    // Initializes the optional text overlay for the output format.
    // The window content scale multiplies the native 6x9 font size, rounded to whole pixels.
    // Uses rasterization or a compute fallback, depending on the device and target.
    Result initOverlay(IDevice* device, Format format);

    // Clears accumulated overlay text. Call before composing a new frame's text, even if drawing is skipped.
    void clearOverlay();

    // Prints the common shortcuts as the first overlay line. Call after clearOverlay(),
    // before appending example-specific controls and information.
    void printOverlayHeader();

    // Prints the shared CameraController shortcuts.
    void printCameraControls();

    // Appends printf-formatted text; include explicit newlines between lines or blocks.
    // Does nothing when hidden or uninitialized. Failed formatting is skipped without changing accumulated text.
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 2, 3)))
#endif
    void
    printOverlay(const char* format, ...);

    // Draws the picker when open, otherwise the accumulated overlay text, over the existing image.
    // Call after the example has finished rendering, before submission.
    // The picker ignores normal overlay visibility. The image must use the initialized output format.
    Result drawOverlay(ICommandEncoder* encoder, ITexture* image);

    // Returns whether the normal overlay is enabled, independently of the picker and initialization.
    bool isOverlayVisible() const { return m_overlayVisible; }
    // Toggles overlay visibility without creating or destroying its resources.
    void toggleOverlay() { m_overlayVisible = !m_overlayVisible; }

public:
    DeviceType m_deviceType = DeviceType::Default;
    std::vector<Feature> m_missingFeatures;
    GLFWwindow* m_window = nullptr;

    float m_mousePos[2] = {0.0f, 0.0f};
    bool m_mouseDown[3] = {false, false, false};

private:
    Result updateWindowTitle();
    Result applyVsync();

    const ExampleDesc* m_desc = nullptr;
    std::string m_windowTitle;
    FrameStats m_frameStats;
    bool m_vsync = true;
    bool m_vsyncChanged = false;
    ComPtr<ISurface> m_presentationSurface;
    ComPtr<ICommandQueue> m_presentationQueue;
    detail::ExampleRunner* m_runner = nullptr;
    int m_windowWidth = 0;
    int m_windowHeight = 0;
    int m_framebufferWidth = 0;
    int m_framebufferHeight = 0;
    float m_contentScale = 1.f; // Use the X scale uniformly to preserve the bitmap font's aspect ratio.
    bool m_minimized = false;
    bool m_overlayVisible = true;
    std::string m_examplePickerText;
    std::unique_ptr<TextRenderer> m_textRenderer;
    std::unique_ptr<Profiler> m_profiler;

    friend struct detail::ExampleRunner;
};

// A stable identifier, display name, and factory shared by standalone and combined executables.
// Capability checks remain in init(), so there is only one source of feature requirements.
struct ExampleDesc
{
    const char* name;
    const char* title;
    const char* shaderSearchPath;
    ExampleBase* (*create)();
};

namespace detail {
inline std::vector<ExampleDesc>& getRegisteredExamples()
{
    static std::vector<ExampleDesc> examples;
    return examples;
}

struct ExampleRegistration
{
    explicit ExampleRegistration(ExampleDesc desc) { getRegisteredExamples().push_back(desc); }
};

inline void glfwWindowPosCallback(GLFWwindow* window, int xpos, int ypos);
inline void glfwWindowSizeCallback(GLFWwindow* window, int width, int height);
inline void glfwWindowIconifyCallback(GLFWwindow* window, int iconified);
inline void glfwWindowMaximizeCallback(GLFWwindow* window, int maximized);
inline void glfwFramebufferSizeCallback(GLFWwindow* window, int width, int height);
inline void glfwWindowContentScaleCallback(GLFWwindow* window, float xscale, float yscale);
inline void glfwCursorPosCallback(GLFWwindow* window, double xpos, double ypos);
inline void glfwCursorEnterCallback(GLFWwindow* window, int entered);
inline void glfwMouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
inline void glfwScrollCallback(GLFWwindow* window, double xoffset, double yoffset);
inline void glfwKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);

// Window handles and the runner list are accessed only on the main thread.
// Example objects and their GPU resources are owned exclusively by their worker.
struct ExampleRunner
{
    struct Event
    {
        enum Type
        {
            Resize,
            MousePosition,
            Input
        } type;
        std::function<Result(ExampleBase&)> dispatch;
    };

    GLFWwindow* window = nullptr;
    bool iconified = false; // Main thread only.
    std::thread thread;
    bool syncFrames = true; // Set before starting the worker.
    std::atomic<bool> stopping = false;
    std::atomic<bool> done = true;
    std::atomic<bool> frameReady = false;
    // Read by the main thread only after joining.
    bool initialized = false;
    int exitCode = 0;
    std::mutex eventMutex;
    std::condition_variable eventReady;
    std::vector<Event> events;
    // A frame's input and timestamp are sealed by the main thread before waking its worker.
    std::vector<Event> frameEvents;
    double frameTime = 0;
    bool framePending = false;

    void postEvent(Event event)
    {
        std::lock_guard<std::mutex> lock(eventMutex);
        if (stopping || done)
            return;
        // Coalesce only adjacent snapshots, preserving ordering with key/button transitions.
        if (!events.empty() && events.back().type == event.type &&
            (event.type == Event::Resize || event.type == Event::MousePosition))
            events.back() = std::move(event);
        else
            events.push_back(std::move(event));
        eventReady.notify_one();
    }

    void stop()
    {
        // Use the event mutex so a sleeping worker cannot miss this notification.
        std::lock_guard<std::mutex> lock(eventMutex);
        stopping = true;
        eventReady.notify_one();
    }

    void setExamplePickerText(std::string text)
    {
        postEvent(
            {Event::Input,
             [text = std::move(text)](ExampleBase& example)
             {
                 example.m_examplePickerText = text;
                 return SLANG_OK;
             }}
        );
    }

    void resize()
    {
        int width, height, framebufferWidth, framebufferHeight;
        glfwGetWindowSize(window, &width, &height);
        glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
        postEvent(
            {Event::Resize,
             [=](ExampleBase& example)
             {
                 // Even a return to the previous size can invalidate a swapchain while
                 // this worker is busy (for example, minimize then restore during compilation).
                 example.m_windowWidth = width;
                 example.m_windowHeight = height;
                 example.m_framebufferWidth = framebufferWidth;
                 example.m_framebufferHeight = framebufferHeight;
                 example.m_frameStats.reset();
                 return example.onResize(width, height, framebufferWidth, framebufferHeight);
             }}
        );
    }

    void setContentScale(float scale)
    {
        postEvent(
            {Event::Input,
             [scale](ExampleBase& example)
             {
                 example.m_contentScale = scale;
                 return SLANG_OK;
             }}
        );
    }

    void setIconified(bool value)
    {
        iconified = value;
        postEvent(
            {Event::Input,
             [value](ExampleBase& example)
             {
                 example.m_minimized = value;
                 example.m_frameStats.reset();
                 return SLANG_OK;
             }}
        );
    }

    void startFrame(double time)
    {
        std::lock_guard<std::mutex> lock(eventMutex);
        frameEvents.swap(events);
        frameTime = time;
        frameReady = false;
        framePending = true;
        eventReady.notify_one();
    }

    void run(const ExampleDesc& desc, DeviceType deviceType);
    Result processEvents(ExampleBase& example, const std::vector<Event>& pending);
};

inline std::vector<std::unique_ptr<ExampleRunner>>& getExamples()
{
    static std::vector<std::unique_ptr<ExampleRunner>> examples;
    return examples;
}

// Mutable picker state belongs to the main thread. Workers only read the registry,
// which stays unchanged until all workers and their queued window tasks have finished.
struct ExamplePicker
{
    std::span<const ExampleDesc> registry;
    bool open = false;
    size_t currentIndex = 0;
    size_t selectedIndex = 0;
    std::optional<size_t> pendingIndex;

    bool isAvailable() const { return registry.size() > 1; }

    void reset(std::span<const ExampleDesc> examples = {})
    {
        *this = {};
        registry = examples;
    }

    void updateText()
    {
        std::string text;
        if (open)
        {
            text = "Select example\nUp/Down: Choose | Enter: Open | Esc: Cancel\n\n";
            for (size_t i = 0; i < registry.size(); ++i)
            {
                text += i == selectedIndex ? "> " : "  ";
                text += registry[i].title;
                if (i == currentIndex)
                    text += " (current)";
                text += '\n';
            }
        }
        // Publish text snapshots; workers never read the main-thread picker state.
        for (auto& example : getExamples())
            example->setExamplePickerText(text);
    }

    void show()
    {
        if (open || pendingIndex)
            return;
        open = true;
        selectedIndex = currentIndex;
        updateText();
    }

    void hide()
    {
        open = false;
        updateText();
    }

    bool handleKey(int key, int action)
    {
        if (!open)
            return false;

        switch (key)
        {
        case GLFW_KEY_UP:
        case GLFW_KEY_DOWN:
            if (action == GLFW_PRESS)
            {
                size_t offset = key == GLFW_KEY_UP ? registry.size() - 1 : 1;
                selectedIndex = (selectedIndex + offset) % registry.size();
                updateText();
            }
            return true;
        case GLFW_KEY_ENTER:
        case GLFW_KEY_KP_ENTER:
        case GLFW_KEY_ESCAPE:
            if (action == GLFW_RELEASE)
            {
                hide();
                if (key != GLFW_KEY_ESCAPE && selectedIndex != currentIndex)
                    pendingIndex = selectedIndex;
            }
            return true;
        default:
            // Let controls held before opening finish normally.
            return action != GLFW_RELEASE;
        }
    }
};

inline ExamplePicker examplePicker;

inline ExampleRunner* mainExample = nullptr;
inline std::thread::id mainThread;
inline std::mutex mainTaskMutex;
inline std::vector<std::function<void()>> mainTasks;

// Synchronous handoff: the worker relinquishes access to captured state until the task finishes.
// Never wait for a worker on the main thread while it might be waiting for one of these tasks.
inline Result runOnMainThread(std::function<Result()> function)
{
    if (std::this_thread::get_id() == mainThread)
        return function();
    auto task = std::make_shared<std::packaged_task<Result()>>(std::move(function));
    auto result = task->get_future();
    {
        std::lock_guard<std::mutex> lock(mainTaskMutex);
        mainTasks.push_back(
            [task]()
            {
                (*task)();
            }
        );
    }
    glfwPostEmptyEvent();
    return result.get();
}

inline void processMainTasks()
{
    std::vector<std::function<void()>> tasks;
    {
        std::lock_guard<std::mutex> lock(mainTaskMutex);
        tasks.swap(mainTasks);
    }
    for (auto& task : tasks)
        task();
}

inline void setMainExample(ExampleRunner* example)
{
    if (mainExample == example)
        return;
    if (mainExample && mainExample->window)
        glfwSetWindowAttrib(mainExample->window, GLFW_RESIZABLE, GLFW_FALSE);
    mainExample = example;
    if (mainExample)
        glfwSetWindowAttrib(mainExample->window, GLFW_RESIZABLE, GLFW_TRUE);
}

// Arrival order is nondeterministic; primary selection and layout follow the backend list.
inline void updateMainExample()
{
    for (auto& example : getExamples())
    {
        if (example->window && !example->stopping)
        {
            setMainExample(example.get());
            return;
        }
    }
    setMainExample(nullptr);
}

// Called only by the main thread, between event-pump iterations. It never waits for workers.
inline void startSynchronizedFrame()
{
    bool hasWork = false;
    for (auto& example : getExamples())
    {
        if (example->stopping || example->done)
            continue;
        if (!example->frameReady)
            return;
        std::lock_guard<std::mutex> lock(example->eventMutex);
        hasWork |= !example->window || !example->iconified || !example->events.empty();
    }
    // Sleep in the event pump when every remaining window is minimized and has no pending input.
    if (!hasWork)
        return;
    double time = glfwGetTime();
    for (auto& example : getExamples())
    {
        if (!example->stopping && !example->done)
            example->startFrame(time);
    }
}

inline void layoutWindows(GLFWmonitor* monitor = nullptr);
inline void fitWindowsToMonitor(bool initialLayout = false);

} // namespace detail


inline ExampleBase::ExampleBase() = default;

inline ExampleBase::~ExampleBase()
{
    m_textRenderer.reset();
    m_presentationSurface.setNull();
    m_presentationQueue.setNull();
    destroyWindow();
}

inline Result ExampleBase::createDevice(
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

inline Result ExampleBase::createWindow(IDevice* device)
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

    SLANG_RETURN_ON_FAIL(
        detail::runOnMainThread(
            [&]() -> Result
            {
                if (m_runner->stopping)
                    return SLANG_FAIL;
                glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
                m_window = glfwCreateWindow(
                    detail::kInitialWindowWidth,
                    detail::kInitialWindowHeight,
                    fullTitle,
                    nullptr,
                    nullptr
                );
                if (!m_window)
                    return SLANG_FAIL;
                m_runner->window = m_window;
                glfwSetWindowUserPointer(m_window, m_runner);
                glfwSetWindowPosCallback(m_window, detail::glfwWindowPosCallback);
                glfwSetWindowSizeCallback(m_window, detail::glfwWindowSizeCallback);
                glfwSetWindowIconifyCallback(m_window, detail::glfwWindowIconifyCallback);
                glfwSetWindowMaximizeCallback(m_window, detail::glfwWindowMaximizeCallback);
                glfwSetFramebufferSizeCallback(m_window, detail::glfwFramebufferSizeCallback);
                glfwSetWindowContentScaleCallback(m_window, detail::glfwWindowContentScaleCallback);
                glfwSetCursorPosCallback(m_window, detail::glfwCursorPosCallback);
                glfwSetCursorEnterCallback(m_window, detail::glfwCursorEnterCallback);
                glfwSetMouseButtonCallback(m_window, detail::glfwMouseButtonCallback);
                glfwSetScrollCallback(m_window, detail::glfwScrollCallback);
                glfwSetKeyCallback(m_window, detail::glfwKeyCallback);
                detail::updateMainExample();
                detail::fitWindowsToMonitor(true);
                glfwGetWindowSize(m_window, &m_windowWidth, &m_windowHeight);
                glfwGetFramebufferSize(m_window, &m_framebufferWidth, &m_framebufferHeight);
                glfwGetWindowContentScale(m_window, &m_contentScale, nullptr);
                return SLANG_OK;
            }
        )
    );
    return updateWindowTitle();
}

inline Result ExampleBase::updateWindowTitle()
{
    char status[128];
    snprintf(status, sizeof(status), " | %.1f FPS | VSync: %s", m_frameStats.fps(), m_vsync ? "On" : "Off");
    std::string title = m_windowTitle + status;
    return detail::runOnMainThread(
        [&]() -> Result
        {
            if (!m_window)
                return SLANG_E_INVALID_HANDLE;
            glfwSetWindowTitle(m_window, title.c_str());
            return SLANG_OK;
        }
    );
}

inline void ExampleBase::destroyWindow()
{
    if (!m_window)
        return;
    detail::runOnMainThread(
        [&]() -> Result
        {
            if (detail::mainExample == m_runner)
                detail::setMainExample(nullptr);
            detail::glfwCursorEnterCallback(m_window, GLFW_FALSE);
            glfwSetWindowUserPointer(m_window, nullptr);
            glfwDestroyWindow(m_window);
            m_window = m_runner->window = nullptr;
            detail::updateMainExample();
            detail::layoutWindows();
            return SLANG_OK;
        }
    );
}

inline bool ExampleBase::shouldClose() const
{
    return m_runner && m_runner->stopping;
}

inline Result ExampleBase::createSurface(IDevice* device, Format format, ISurface** outSurface)
{
    ASSERT(m_window, "Window must be created before creating surface");
    SLANG_RETURN_ON_FAIL(device->getQueue(QueueType::Graphics, m_presentationQueue.writeRef()));
    // Native surface creation may modify the Cocoa view. Transfer exclusive device access
    // to the main thread for this operation; subsequent rendering stays on the worker.
    SLANG_RETURN_ON_FAIL(
        detail::runOnMainThread(
            [&]() -> Result
            {
                SLANG_RHI_DEVICE_SCOPE(device);
                glfwGetWindowSize(m_window, &m_windowWidth, &m_windowHeight);
                glfwGetFramebufferSize(m_window, &m_framebufferWidth, &m_framebufferHeight);
                SLANG_RETURN_ON_FAIL(device->createSurface(getWindowHandleFromGLFW(m_window), outSurface));
                SurfaceConfig surfaceConfig;
                surfaceConfig.width = m_framebufferWidth;
                surfaceConfig.height = m_framebufferHeight;
                surfaceConfig.format = format;
                return configureSurface(*outSurface, surfaceConfig);
            }
        )
    );
    m_presentationSurface = *outSurface;
    return SLANG_OK;
}

inline Result ExampleBase::configureSurface(ISurface* surface, SurfaceConfig config)
{
    config.vsync = m_vsync;
    return surface->configure(config);
}

inline Result ExampleBase::applyVsync()
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

inline Result ExampleBase::presentSurface()
{
    SLANG_RETURN_ON_FAIL(m_presentationSurface->present());
    if (m_frameStats.update(glfwGetTime()))
        return updateWindowTitle();
    return SLANG_OK;
}

inline Result ExampleBase::initProfiler(IDevice* device)
{
    auto profiler = std::make_unique<Profiler>();
    SLANG_RETURN_ON_FAIL(profiler->init(device));
    m_profiler = std::move(profiler);
    return SLANG_OK;
}

inline Profiler& ExampleBase::getProfiler()
{
    ASSERT(m_profiler, "Profiler must be initialized before accessing it");
    return *m_profiler;
}

inline void ExampleBase::toggleProfiler()
{
    if (!m_profiler)
        return;
    m_profiler->setEnabled(!m_profiler->enabled());
}

inline Result ExampleBase::initOverlay(IDevice* device, Format format)
{
    m_textRenderer = std::make_unique<TextRenderer>();
    return m_textRenderer->init(device, format);
}

inline void ExampleBase::clearOverlay()
{
    if (m_textRenderer)
        m_textRenderer->clear();
}

inline void ExampleBase::printOverlayHeader()
{
    printOverlay("F1: Overlay | F2: Fit windows | F3: VSync");
    if (m_profiler)
        printOverlay(" | F4: Profiler");
    if (detail::examplePicker.isAvailable())
        printOverlay(" | F5: Pick example");
    printOverlay(" | Esc: Close\n");
}

inline void ExampleBase::printCameraControls()
{
    printOverlay("Left drag: Look | W/A/S/D: Move | Q/E: Down/up | Shift: Faster\n");
}

inline void ExampleBase::printOverlay(const char* format, ...)
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

inline Result ExampleBase::drawOverlay(ICommandEncoder* encoder, ITexture* image)
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

namespace detail {

inline bool layoutInProgress = false;
inline bool syncingWindows = false;
inline int layoutMargin = 100;

// Use the monitor containing most of the primary window, including on desktops
// where monitors have negative coordinates. Fall back if the window is off screen.
inline GLFWmonitor* getExampleMonitor()
{
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    if (!mainExample)
        return monitor;
    int x, y, width, height;
    glfwGetWindowPos(mainExample->window, &x, &y);
    glfwGetWindowSize(mainExample->window, &width, &height);
    int count = 0;
    GLFWmonitor** monitors = glfwGetMonitors(&count);
    int64_t bestOverlap = 0;
    for (int i = 0; i < count; ++i)
    {
        int mx, my, mw, mh;
        glfwGetMonitorWorkarea(monitors[i], &mx, &my, &mw, &mh);
        int overlapWidth = std::max(0, std::min(x + width, mx + mw) - std::max(x, mx));
        int overlapHeight = std::max(0, std::min(y + height, my + mh) - std::max(y, my));
        int64_t overlap = int64_t(overlapWidth) * overlapHeight;
        if (overlap > bestOverlap)
        {
            bestOverlap = overlap;
            monitor = monitors[i];
        }
    }
    return monitor;
}

inline void layoutWindows(GLFWmonitor* monitor)
{
    // Keep a single window freely movable unless explicitly fitting it to a monitor.
    if (!mainExample || layoutInProgress || (!monitor && getExamples().size() <= 1 && layoutMargin != 0))
        return;
    if (!monitor)
        monitor = getExampleMonitor();
    if (!monitor)
        return;
    int wx, wy, ww, wh;
    glfwGetMonitorWorkarea(monitor, &wx, &wy, &ww, &wh);
    // Use equal cells even when the primary and non-resizable windows have
    // different decorations. Window positions refer to the client area.
    int cellWidth = 0, cellHeight = 0;
    for (auto& example : getExamples())
    {
        if (!example->window || example->stopping)
            continue;
        int width, height, left, top, right, bottom;
        glfwGetWindowSize(example->window, &width, &height);
        glfwGetWindowFrameSize(example->window, &left, &top, &right, &bottom);
        cellWidth = std::max(cellWidth, width + left + right);
        cellHeight = std::max(cellHeight, height + top + bottom);
    }
    layoutInProgress = true;
    int x = wx + layoutMargin;
    int y = wy + layoutMargin;
    for (auto& example : getExamples())
    {
        if (!example->window || example->stopping)
            continue;
        int left, top, right, bottom;
        glfwGetWindowFrameSize(example->window, &left, &top, &right, &bottom);
        if (x > wx + layoutMargin && x + cellWidth > wx + ww - layoutMargin)
        {
            x = wx + layoutMargin;
            y += cellHeight;
        }
        glfwSetWindowPos(example->window, x + left, y + top);
        x += cellWidth;
    }
    layoutInProgress = false;
}

inline void fitWindowsToMonitor(bool initialLayout)
{
    if (!mainExample || layoutInProgress || syncingWindows)
        return;
    GLFWmonitor* monitor = getExampleMonitor();
    if (!monitor)
        return;
    int wx, wy, ww, wh;
    glfwGetMonitorWorkarea(monitor, &wx, &wy, &ww, &wh);
    if (ww <= 0 || wh <= 0)
        return;

    // F2 restores before measuring decorations, suppressing recursive window sync.
    // Resize callbacks still queue the new framebuffer sizes for the workers.
    layoutInProgress = syncingWindows = true;
    int count = 0, frameWidth = 0, frameHeight = 0;
    for (auto& example : getExamples())
    {
        if (!example->window || example->stopping)
            continue;
        if (!initialLayout)
            glfwRestoreWindow(example->window);
        int left, top, right, bottom;
        glfwGetWindowFrameSize(example->window, &left, &top, &right, &bottom);
        frameWidth = std::max(frameWidth, left + right);
        frameHeight = std::max(frameHeight, top + bottom);
        ++count;
    }

    int width = kInitialWindowWidth, height = kInitialWindowHeight;
    if (!initialLayout)
        glfwGetWindowSize(mainExample->window, &width, &height);
    width = std::max(width, 1);
    height = std::max(height, 1);
    int margin = initialLayout ? std::min({100, ww / 10, wh / 10}) : 0;
    int bestWidth = 0, bestHeight = 0;
    int64_t bestArea = 0;
    // Try every uniform grid and maximize render area while preserving the
    // current aspect ratio. This avoids filling the screen with narrow strips.
    for (int columns = 1; columns <= count; ++columns)
    {
        int rows = (count + columns - 1) / columns;
        int availableWidth = (ww - 2 * margin) / columns - frameWidth;
        int availableHeight = (wh - 2 * margin) / rows - frameHeight;
        if (initialLayout)
        {
            availableWidth = std::min(availableWidth, width);
            availableHeight = std::min(availableHeight, height);
        }
        if (availableWidth <= 0 || availableHeight <= 0)
            continue;
        int h = std::min(availableHeight, int(int64_t(availableWidth) * height / width));
        int w = std::min(availableWidth, int(int64_t(h) * width / height));
        int64_t area = int64_t(w) * h;
        if (area > bestArea)
        {
            bestArea = area;
            bestWidth = w;
            bestHeight = h;
        }
    }
    if (bestArea > 0)
    {
        layoutMargin = margin;
        for (auto& example : getExamples())
        {
            if (example->window && !example->stopping)
                glfwSetWindowSize(example->window, bestWidth, bestHeight);
        }
    }
    layoutInProgress = syncingWindows = false;
    layoutWindows(monitor);
}

inline void glfwWindowPosCallback(GLFWwindow* window, int, int)
{
    if (glfwGetWindowUserPointer(window) == mainExample)
        layoutWindows();
}

inline void glfwWindowSizeCallback(GLFWwindow* window, int width, int height)
{
    auto* source = static_cast<ExampleRunner*>(glfwGetWindowUserPointer(window));
    source->resize();
    if (syncingWindows || source != mainExample)
        return;
    syncingWindows = true;
    for (auto& example : getExamples())
    {
        if (example->window && example.get() != source)
            glfwSetWindowSize(example->window, width, height);
    }
    syncingWindows = false;
    layoutWindows();
}

inline void glfwWindowIconifyCallback(GLFWwindow* window, int iconified)
{
    static_cast<ExampleRunner*>(glfwGetWindowUserPointer(window))->setIconified(iconified != 0);
    if (syncingWindows)
        return;
    syncingWindows = true;
    for (auto& example : getExamples())
    {
        if (example->window && example->window != window &&
            glfwGetWindowAttrib(example->window, GLFW_ICONIFIED) != iconified)
        {
            if (iconified)
                glfwIconifyWindow(example->window);
            else
                glfwRestoreWindow(example->window);
        }
    }
    syncingWindows = false;
}

inline void glfwWindowMaximizeCallback(GLFWwindow* window, int maximized)
{
    if (syncingWindows)
        return;
    syncingWindows = true;
    for (auto& example : getExamples())
    {
        if (example->window && example->window != window &&
            glfwGetWindowAttrib(example->window, GLFW_MAXIMIZED) != maximized)
        {
            if (maximized)
                glfwMaximizeWindow(example->window);
            else
                glfwRestoreWindow(example->window);
        }
    }
    syncingWindows = false;
}

inline void glfwFramebufferSizeCallback(GLFWwindow* window, int, int)
{
    static_cast<ExampleRunner*>(glfwGetWindowUserPointer(window))->resize();
}

inline void glfwWindowContentScaleCallback(GLFWwindow* window, float xscale, float)
{
    static_cast<ExampleRunner*>(glfwGetWindowUserPointer(window))->setContentScale(xscale);
}

inline void broadcastEvent(ExampleRunner::Event event)
{
    for (auto& example : getExamples())
        example->postEvent(event);
}

inline void glfwCursorPosCallback(GLFWwindow*, double x, double y)
{
    broadcastEvent(
        {ExampleRunner::Event::MousePosition,
         [=](ExampleBase& example)
         {
             example.m_mousePos[0] = float(x);
             example.m_mousePos[1] = float(y);
             example.onMousePosition(float(x), float(y));
             return SLANG_OK;
         }}
    );
}

inline void glfwCursorEnterCallback(GLFWwindow*, int entered)
{
    broadcastEvent(
        {ExampleRunner::Event::Input,
         [entered](ExampleBase& example)
         {
             example.onMouseEnter(entered != 0);
             return SLANG_OK;
         }}
    );
}

inline void glfwMouseButtonCallback(GLFWwindow*, int button, int action, int mods)
{
    broadcastEvent(
        {ExampleRunner::Event::Input,
         [=](ExampleBase& example)
         {
             if (button >= 0 && button < SLANG_COUNT_OF(example.m_mouseDown))
                 example.m_mouseDown[button] = action == GLFW_PRESS;
             example.onMouseButton(button, action, mods);
             return SLANG_OK;
         }}
    );
}

inline void glfwScrollCallback(GLFWwindow*, double x, double y)
{
    broadcastEvent(
        {ExampleRunner::Event::Input,
         [=](ExampleBase& example)
         {
             example.onScroll(float(x), float(y));
             return SLANG_OK;
         }}
    );
}

inline void glfwKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    if (examplePicker.handleKey(key, action))
        return;
    if (key == GLFW_KEY_F2 && action == GLFW_PRESS)
    {
        fitWindowsToMonitor();
        return;
    }
    if (key == GLFW_KEY_F5 && action == GLFW_PRESS && examplePicker.isAvailable())
    {
        examplePicker.show();
        return;
    }
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
    {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
        return;
    }
    broadcastEvent(
        {ExampleRunner::Event::Input,
         [=](ExampleBase& example)
         {
             if (key == GLFW_KEY_F1)
             {
                 if (action == GLFW_PRESS)
                     example.toggleOverlay();
             }
             else if (key == GLFW_KEY_F3)
             {
                 if (action == GLFW_PRESS)
                     example.toggleVsync();
             }
             else if (key == GLFW_KEY_F4)
             {
                 if (action == GLFW_PRESS)
                     example.toggleProfiler();
             }
             else
                 example.onKey(key, scancode, action, mods);
             return SLANG_OK;
         }}
    );
}

inline Result ExampleRunner::processEvents(ExampleBase& example, const std::vector<Event>& pending)
{
    for (const Event& event : pending)
    {
        if (stopping)
            break;
        SLANG_RETURN_ON_FAIL(event.dispatch(example));
    }
    return SLANG_OK;
}

inline void ExampleRunner::run(const ExampleDesc& desc, DeviceType deviceType)
{
    const char* name = getRHI()->getDeviceTypeName(deviceType);
    std::unique_ptr<ExampleBase> example(desc.create());
    example->m_runner = this;
    example->m_desc = &desc;
    Result result = example->init(deviceType);
    initialized = SLANG_SUCCEEDED(result);
    if (SLANG_FAILED(result) && !stopping)
    {
        if (result == SLANG_E_NOT_AVAILABLE && !example->m_missingFeatures.empty())
        {
            std::string missing;
            for (Feature feature : example->m_missingFeatures)
            {
                if (!missing.empty())
                    missing += ", ";
                missing += getRHI()->getFeatureName(feature);
            }
            LOG_INFO("%s: skipped (missing required features: %s).", name, missing.c_str());
        }
        else
        {
            LOG_ERROR("Could not initialize %s example (0x%08x).", name, unsigned(result));
            exitCode = 1;
        }
    }
    while (initialized && !stopping)
    {
        std::vector<Event> pending;
        double time = 0;
        {
            std::unique_lock<std::mutex> lock(eventMutex);
            if (syncFrames)
            {
                // Acknowledge initialization/the previous frame, then wait for a common frame grant.
                frameReady = true;
                glfwPostEmptyEvent();
                eventReady.wait(
                    lock,
                    [&]()
                    {
                        return stopping || framePending;
                    }
                );
                if (stopping)
                    break;
                framePending = false;
                pending.swap(frameEvents);
                time = frameTime;
            }
            else
                pending.swap(events);
        }
        result = processEvents(*example, pending);
        if (SLANG_FAILED(result))
        {
            LOG_ERROR("%s: example event failed (0x%08x).", name, unsigned(result));
            exitCode = 1;
            break;
        }
        if (stopping)
            break;
        result = example->applyVsync();
        if (SLANG_FAILED(result))
        {
            LOG_ERROR("%s: could not apply VSync (0x%08x).", name, unsigned(result));
            exitCode = 1;
            break;
        }
        bool minimized = example->m_window && (example->m_minimized || example->m_framebufferWidth == 0 ||
                                               example->m_framebufferHeight == 0);
        if (minimized && !syncFrames)
        {
            std::unique_lock<std::mutex> lock(eventMutex);
            eventReady.wait(
                lock,
                [&]()
                {
                    return stopping || !events.empty();
                }
            );
            continue;
        }
        // GLFW's timer may be read from any thread; every example uses the same time origin.
        if (!syncFrames)
            time = glfwGetTime();
        result = example->update(time);
        if (SLANG_SUCCEEDED(result) && !stopping && !minimized)
            result = example->draw();
        if (SLANG_FAILED(result))
        {
            LOG_ERROR("%s: example frame failed (0x%08x).", name, unsigned(result));
            exitCode = 1;
            break;
        }
    }
    stop();
    example->shutdown();
    // Destroy all GPU resources on their owning thread, then destroy the window via the main thread.
    example.reset();
    done = true;
    glfwPostEmptyEvent();
}

struct ExampleRoundResult
{
    bool anyInitialized = false;
    bool closing = false;
    int exitCode = 0;
};

// Run one selection across all backends, servicing main-thread tasks through shutdown.
inline ExampleRoundResult runExampleRound(
    const ExampleDesc& desc,
    std::span<const DeviceType> deviceTypes,
    bool syncFrames
)
{
    auto& examples = getExamples();
    glfwSetTime(0);
    LOG_INFO("Example: %s", desc.title);
    // Finish the main-thread registry before any worker can request a window.
    for (size_t i = 0; i < deviceTypes.size(); ++i)
        examples.push_back(std::make_unique<ExampleRunner>());
    for (size_t i = 0; i < examples.size(); ++i)
    {
        auto& example = *examples[i];
        example.done = false;
        example.syncFrames = syncFrames;
        example.thread = std::thread(&ExampleRunner::run, &example, std::cref(desc), deviceTypes[i]);
    }

    // Service window requests during initialization AND shutdown. A switch only
    // asks workers to stop: joining before pumping these tasks would deadlock.
    ExampleRoundResult result;
    for (;;)
    {
        // Observe close requests before queued tasks can destroy their windows.
        for (auto& example : examples)
            if (example->window && glfwWindowShouldClose(example->window))
                result.closing = true;
        processMainTasks();
        if (result.closing || examplePicker.pendingIndex)
        {
            for (auto& example : examples)
                if (!example->stopping)
                    example->stop();
        }
        bool allDone = true;
        for (auto& example : examples)
            allDone &= example->done.load();
        if (allDone)
            break;
        if (syncFrames)
            startSynchronizedFrame();
        glfwWaitEventsTimeout(0.01);
        if (!mainExample)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    for (auto& example : examples)
    {
        if (example->thread.joinable())
            example->thread.join();
        result.exitCode |= example->exitCode;
        result.anyInitialized |= example->initialized;
    }
    examples.clear();
    mainExample = nullptr;
    examplePicker.hide();
    return result;
}

// By default, wait for all workers before each common frame, including its input batch and
// timestamp, while keeping the GLFW event pump active. --free-running lets workers advance independently.
inline int runExamples(
    int argc,
    const char** argv,
    std::span<const ExampleDesc> registry,
    std::string_view defaultExample = {}
)
{
    ASSERT(!registry.empty(), "The example registry must not be empty.");

    std::vector<DeviceType> deviceTypes = {
        DeviceType::D3D11,
        DeviceType::D3D12,
        DeviceType::Vulkan,
        DeviceType::Metal,
        DeviceType::CPU,
        DeviceType::CUDA,
        DeviceType::WGPU,
    };

    auto findExample = [&](std::string_view name) -> std::optional<size_t>
    {
        for (size_t i = 0; i < registry.size(); ++i)
            if (name == registry[i].name)
                return i;
        return std::nullopt;
    };

    size_t initialIndex = findExample(defaultExample).value_or(0);
    bool syncFrames = true;
    bool deviceSelected = false;
    bool exampleSelected = false;
    bool listExamples = false;

    for (int i = 1; i < argc; ++i)
    {
        std::string option = argv[i];
        if (option == "--list-examples")
        {
            listExamples = true;
            continue;
        }
        if (option == "--example" && !exampleSelected && i + 1 < argc)
        {
            exampleSelected = true;
            auto selected = findExample(argv[++i]);
            if (!selected)
            {
                LOG_ERROR("Unknown example: %s (use --list-examples).", argv[i]);
                return 1;
            }
            initialIndex = *selected;
            continue;
        }
        if (option == "--free-running" && syncFrames)
        {
            syncFrames = false;
            continue;
        }
        if (option != "--device" || deviceSelected || i + 1 == argc)
        {
            LOG_ERROR(
                "Usage: %s [--example <name>] [--list-examples] [--device <d3d11|d3d12|vulkan|metal|cpu|cuda|wgpu>] "
                "[--free-running]",
                argv[0]
            );
            return 1;
        }
        deviceSelected = true;
        std::string requestedDevice = argv[++i];
        auto toLower = [](unsigned char c)
        {
            return char(std::tolower(c));
        };
        std::transform(requestedDevice.begin(), requestedDevice.end(), requestedDevice.begin(), toLower);
        auto selectedDevice = std::find_if(
            deviceTypes.begin(),
            deviceTypes.end(),
            [&](DeviceType type)
            {
                std::string name = getRHI()->getDeviceTypeName(type);
                std::transform(name.begin(), name.end(), name.begin(), toLower);
                return name == requestedDevice;
            }
        );
        if (selectedDevice == deviceTypes.end())
        {
            LOG_ERROR("Unknown device: %s", argv[i]);
            return 1;
        }
        deviceTypes = {*selectedDevice};
    }

    if (listExamples)
    {
        for (const auto& desc : registry)
            std::printf("%s: %s\n", desc.name, desc.title);
        return 0;
    }

    examplePicker.reset(registry);
    if (!glfwInit())
    {
        LOG_ERROR("Failed to initialize GLFW");
        return 1;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    mainThread = std::this_thread::get_id();
#if SLANG_RHI_DEBUG
    // Global RHI configuration must be set before any worker creates a device.
    DebugLayerOptions debugLayerOptions;
    debugLayerOptions.coreValidation = true;
    getRHI()->setDebugLayerOptions(debugLayerOptions);
#endif
    int exitCode = 0;
    std::vector<DeviceType> supportedTypes;
    for (DeviceType deviceType : deviceTypes)
    {
        if (getRHI()->isDeviceTypeSupported(deviceType))
        {
            // Prime discovery once, before concurrent device creation.
            getRHI()->getAdapter(deviceType, 0);
            supportedTypes.push_back(deviceType);
        }
        else
            LOG_INFO("%s: skipped (backend not enabled in this build).", getRHI()->getDeviceTypeName(deviceType));
    }
    std::optional<size_t> lastWorkingIndex;
    size_t activeIndex = initialIndex;
    for (;;)
    {
        examplePicker.currentIndex = activeIndex;
        examplePicker.pendingIndex.reset();
        layoutMargin = 100;
        const auto& desc = registry[activeIndex];
        auto result = runExampleRound(desc, supportedTypes, syncFrames);
        exitCode |= result.exitCode;
        if (result.anyInitialized)
            lastWorkingIndex = activeIndex;
        if (result.closing)
            break;

        auto nextIndex = examplePicker.pendingIndex;
        if (!result.anyInitialized && !nextIndex)
        {
            // A runtime selection unsupported by every backend must not strand the user
            // without a window. An unsupported initial selection still fails normally.
            if (lastWorkingIndex && *lastWorkingIndex != activeIndex)
            {
                LOG_WARNING("No backend could run %s; returning to %s.", desc.title, registry[*lastWorkingIndex].title);
                nextIndex = lastWorkingIndex;
            }
            else
                exitCode = 1;
        }
        if (!nextIndex)
            break;
        activeIndex = *nextIndex;
    }
    examplePicker.reset();
    glfwTerminate();

    return exitCode;
}

inline int runRegisteredExamples(int argc, const char** argv, std::string_view defaultExample = {})
{
    auto& registry = getRegisteredExamples();
    // Static registration order varies across translation units.
    std::sort(
        registry.begin(),
        registry.end(),
        [](const ExampleDesc& a, const ExampleDesc& b)
        {
            return std::string_view(a.name) < std::string_view(b.name);
        }
    );
    return runExamples(argc, argv, registry, defaultExample);
}

} // namespace detail

} // namespace rhi

// Each translation unit registers its descriptor without constructing an example.
// Only standalone builds define main().
#if defined(EXAMPLE_COMBINED)
#define EXAMPLE_STANDALONE_MAIN()
#else
#define EXAMPLE_STANDALONE_MAIN()                                                                                      \
    int main(int argc, const char** argv)                                                                              \
    {                                                                                                                  \
        return rhi::detail::runRegisteredExamples(argc, argv);                                                         \
    }
#endif

#define EXAMPLE_MAIN(Example, Name, Title)                                                                             \
    namespace {                                                                                                        \
    const rhi::detail::ExampleRegistration register##Example({                                                         \
        Name,                                                                                                          \
        Title,                                                                                                         \
        EXAMPLE_DIR,                                                                                                   \
        []() -> rhi::ExampleBase*                                                                                      \
        {                                                                                                              \
            return new Example();                                                                                      \
        },                                                                                                             \
    });                                                                                                                \
    }                                                                                                                  \
    EXAMPLE_STANDALONE_MAIN()
