#pragma once

// Private implementation shared by the base helpers and application runner.
#include "example.h"

#include <functional>
#include <optional>
#include <span>

#if !SLANG_WASM
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#endif

namespace rhi::detail {

constexpr int kInitialWindowWidth = 1280;
constexpr int kInitialWindowHeight = 720;

// One list drives input handling, overlay labels, and browser shortcut suppression.
struct ExampleShortcut
{
    int key;
    const char* keyName;
    const char* label;
    void (ExampleBase::*callback)() = nullptr;
};

inline constexpr ExampleShortcut kExampleShortcuts[] = {
    {GLFW_KEY_F1, "F1", "Overlay", &ExampleBase::toggleOverlay},
#if !SLANG_WASM
    {GLFW_KEY_F2, "F2", "Fit windows"},
    {GLFW_KEY_F3, "F3", "VSync", &ExampleBase::toggleVsync},
#endif
    {GLFW_KEY_F4, "F4", "Profiler", &ExampleBase::toggleProfiler},
    {GLFW_KEY_F5, "F5", "Pick example"},
    {GLFW_KEY_ESCAPE, "Escape", "Close"},
};

class ExampleApp;

// Window handles and the runner list are accessed only on the main thread.
// Examples and GPU resources belong to a native worker or the browser main thread.
struct ExampleRunner
{
    explicit ExampleRunner(ExampleApp& app)
        : app(app)
    {
    }

    ExampleApp& app;

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
    // Result fields are read after run() returns (after joining on native).
    bool initialized = false;
    int exitCode = 0;
    std::vector<Event> events;

#if SLANG_WASM
    bool stopping = false;
    bool done = true;
    bool isSynchronized() const { return false; }
#else
    std::thread thread;
    bool syncFrames = true; // Set before starting the worker.
    std::atomic<bool> stopping = false;
    std::atomic<bool> done = true;
    std::atomic<bool> frameReady = false;
    std::mutex eventMutex;
    std::condition_variable eventReady;
    // A frame's input and timestamp are sealed by the main thread before waking its worker.
    std::vector<Event> frameEvents;
    double frameTime = 0;
    bool framePending = false;

    bool isSynchronized() const { return syncFrames; }
    void startFrame(double time);
#endif

    void run(const ExampleDesc& desc, DeviceType deviceType);
    void stop();

    Result runFrames(ExampleBase& example, DeviceType deviceType);
    // Collect input and, when synchronized, wait for a shared frame timestamp.
    // Returns false when stopping.
    bool prepareFrame(std::vector<Event>& pending, double& time);
    void waitWhileMinimized();
    void finishFrame();

    void postEvent(Event event);
    Result processEvents(ExampleBase& example, const std::vector<Event>& pending);
    // Called with the native event mutex held; browser callbacks never dispatch directly.
    void queueEvent(Event event);
    void complete();

    void resize();
    void setContentScale(float scale);
    void setIconified(bool value);
    void setExamplePickerText(std::string text);
};

// The picker only tracks navigation and produces a selection. Application state
// and publishing its text to the runners belong to ExampleApp.
struct ExamplePicker
{
    struct KeyResult
    {
        bool handled = false;
        std::optional<size_t> selection = std::nullopt;
    };

    bool open = false;
    size_t selectedIndex = 0;

    std::string getText(std::span<const ExampleDesc> registry, size_t currentIndex) const;
    void show(size_t currentIndex);
    void hide() { open = false; }
    KeyResult handleKey(int key, int action, size_t exampleCount);
};

struct ExampleRoundResult
{
    bool anyInitialized = false;
    int exitCode = 0;
};

// One application owns all state for its invocation. Runners and GLFW callbacks
// retain a reference to it until every worker has finished and its window is gone.
class ExampleApp
{
public:
    explicit ExampleApp(std::span<const ExampleDesc> registry)
        : m_registry(registry)
    {
    }

    int run(int argc, const char** argv, std::string_view defaultExample);

private:
    // Selection and window state are accessed only on the main thread. Workers
    // read the immutable registry and use runOnMainThread() for window operations.
    std::span<const ExampleDesc> m_registry;
    std::vector<std::unique_ptr<ExampleRunner>> m_examples;
    ExamplePicker m_picker;
    size_t m_currentIndex = 0;
    std::optional<size_t> m_pendingIndex;
    std::optional<size_t> m_lastWorkingIndex;
    bool m_closing = false;
    ExampleRunner* m_mainExample = nullptr;
    bool m_syncingWindows = false;

#if SLANG_WASM
    void processBrowserRequests();
#else
    bool m_layoutInProgress = false;
    int m_layoutMargin = 100;

    const std::thread::id m_mainThread = std::this_thread::get_id();
    std::mutex m_mainTaskMutex;
    std::vector<std::function<void()>> m_mainTasks;

    void processMainTasks();
    void startSynchronizedFrame();
    GLFWmonitor* getExampleMonitor();
#endif

    static std::vector<DeviceType> getDeviceTypes();
    bool initialize();
    void shutdown();

    // Notifications do not control selection or cleanup. Native implementations are no-ops.
    enum class State
    {
        Loading,
        Running,
        Stopping,
        Stopped,
        Failed
    };
    void notifyExamplesReady();
    void notifyExampleChanged();
    void notifyState(State state);

    ExampleRoundResult runExampleRound(
        const ExampleDesc& desc,
        std::span<const DeviceType> deviceTypes,
        bool syncFrames
    );
    void runExamples(const ExampleDesc& desc, std::span<const DeviceType> deviceTypes, bool syncFrames);

    bool hasExamplePicker() const { return m_registry.size() > 1; }
    void requestExample(size_t index);
    void requestClose();
    void stopRequestedExamples();
    void updatePickerText();

    Result runOnMainThread(std::function<Result()> function);

    // Main-thread only. The caller releases GPU resources before destroying the window.
    GLFWwindow* createWindow(ExampleRunner& runner, const char* title, const char* adapterName);
    void destroyWindow(ExampleRunner& runner);
    WindowHandle getWindowHandle(const ExampleRunner& runner);
    void updateWindowTitle(const ExampleRunner& runner, const char* title, double fps, bool vsync);

    void setMainExample(ExampleRunner* example);
    void updateMainExample();
    void layoutWindows(GLFWmonitor* monitor = nullptr);
    void fitWindowsToMonitor(bool initialLayout = false);
    void syncWindowState(GLFWwindow* window, int attribute, int value, void (*enable)(GLFWwindow*));

    void broadcastEvent(ExampleRunner::Event event);
    void handleKey(GLFWwindow* window, int key, int scancode, int action, int mods);

    static ExampleRunner& getWindowRunner(GLFWwindow* window)
    {
        return *static_cast<ExampleRunner*>(glfwGetWindowUserPointer(window));
    }
    static void glfwWindowCloseCallback(GLFWwindow* window);
    static void glfwWindowPosCallback(GLFWwindow* window, int xpos, int ypos);
    static void glfwWindowSizeCallback(GLFWwindow* window, int width, int height);
    static void glfwWindowIconifyCallback(GLFWwindow* window, int iconified);
    static void glfwWindowMaximizeCallback(GLFWwindow* window, int maximized);
    static void glfwFramebufferSizeCallback(GLFWwindow* window, int width, int height);
    static void glfwWindowContentScaleCallback(GLFWwindow* window, float xscale, float yscale);
    static void glfwCursorPosCallback(GLFWwindow* window, double xpos, double ypos);
    static void glfwCursorEnterCallback(GLFWwindow* window, int entered);
    static void glfwMouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void glfwScrollCallback(GLFWwindow* window, double xoffset, double yoffset);
    static void glfwKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);

    friend class ::rhi::ExampleBase;
    friend struct ExampleRunner;
};


} // namespace rhi::detail
