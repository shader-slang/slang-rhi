#include "example-runner.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

#if SLANG_WASM
#include "example-browser.h"
#include <sys/stat.h>
#else
#include <chrono>
#include <future>

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
#endif

namespace rhi::detail {

namespace {

GLFWwindow* createExampleWindow(const char* title, const char* adapterName);

std::vector<ExampleDesc>& getRegisteredExamples()
{
    static std::vector<ExampleDesc> examples;
    return examples;
}

struct ExampleOptions
{
    size_t initialIndex = 0;
    bool syncFrames = true;
    bool listExamples = false;
    std::vector<DeviceType> deviceTypes;
};

std::optional<ExampleOptions> parseExampleOptions(
    int argc,
    const char** argv,
    std::span<const ExampleDesc> registry,
    std::string_view defaultExample,
    std::vector<DeviceType> deviceTypes
)
{
    auto findExample = [&](std::string_view name) -> std::optional<size_t>
    {
        for (size_t i = 0; i < registry.size(); ++i)
            if (name == registry[i].name)
                return i;
        return std::nullopt;
    };

    ExampleOptions options;
    options.initialIndex = findExample(defaultExample).value_or(0);
    options.deviceTypes = std::move(deviceTypes);
    bool deviceSelected = false;
    bool exampleSelected = false;

    for (int i = 1; i < argc; ++i)
    {
        std::string option = argv[i];
        if (option == "--list-examples")
        {
            options.listExamples = true;
            continue;
        }
        if (option == "--example" && !exampleSelected && i + 1 < argc)
        {
            exampleSelected = true;
            auto selected = findExample(argv[++i]);
            if (!selected)
            {
                LOG_ERROR("Unknown example: %s (use --list-examples).", argv[i]);
                return std::nullopt;
            }
            options.initialIndex = *selected;
            continue;
        }
        if (option == "--free-running" && options.syncFrames)
        {
            options.syncFrames = false;
            continue;
        }
        if (option != "--device" || deviceSelected || i + 1 == argc)
        {
            LOG_ERROR(
                "Usage: %s [--example <name>] [--list-examples] [--device <d3d11|d3d12|vulkan|metal|cpu|cuda|wgpu>] "
                "[--free-running]",
                argv[0]
            );
            return std::nullopt;
        }
        deviceSelected = true;
        std::string requestedDevice = argv[++i];
        auto toLower = [](unsigned char c)
        {
            return char(std::tolower(c));
        };
        std::transform(requestedDevice.begin(), requestedDevice.end(), requestedDevice.begin(), toLower);
        auto selectedDevice = std::find_if(
            options.deviceTypes.begin(),
            options.deviceTypes.end(),
            [&](DeviceType type)
            {
                std::string name = getRHI()->getDeviceTypeName(type);
                std::transform(name.begin(), name.end(), name.begin(), toLower);
                return name == requestedDevice;
            }
        );
        if (selectedDevice == options.deviceTypes.end())
        {
            LOG_ERROR("Unknown device: %s", argv[i]);
            return std::nullopt;
        }
        options.deviceTypes = {*selectedDevice};
    }
    return options;
}

} // namespace

ExampleRegistration::ExampleRegistration(ExampleDesc desc)
{
    getRegisteredExamples().push_back(desc);
}

std::string ExamplePicker::getText(std::span<const ExampleDesc> registry, size_t currentIndex) const
{
    std::string text;
    if (open)
    {
        text = "^5Select example^0\n^3Up/Down^0: Choose | ^3Enter^0: Open | ^3Esc^0: Cancel\n\n";
        for (size_t i = 0; i < registry.size(); ++i)
        {
            text += i == selectedIndex ? "^3> " : "  ";
            text += registry[i].title;
            if (i == currentIndex)
                text += "^2 (current)";
            text += "^0\n";
        }
    }
    return text;
}

void ExamplePicker::show(size_t currentIndex)
{
    open = true;
    selectedIndex = currentIndex;
}

ExamplePicker::KeyResult ExamplePicker::handleKey(int key, int action, size_t exampleCount)
{
    if (!open)
        return {};

    switch (key)
    {
    case GLFW_KEY_UP:
    case GLFW_KEY_DOWN:
        if (action == GLFW_PRESS)
        {
            size_t offset = key == GLFW_KEY_UP ? exampleCount - 1 : 1;
            selectedIndex = (selectedIndex + offset) % exampleCount;
        }
        return {true};
    case GLFW_KEY_ENTER:
    case GLFW_KEY_KP_ENTER:
    case GLFW_KEY_ESCAPE:
        if (action == GLFW_RELEASE)
        {
            hide();
            if (key != GLFW_KEY_ESCAPE)
                return {true, selectedIndex};
        }
        return {true};
    default:
        // Let controls held before opening finish normally.
        return {action != GLFW_RELEASE};
    }
}

// Selection, fallback, and cleanup are shared by the native and browser runners.
int ExampleApp::run(int argc, const char** argv, std::string_view defaultExample)
{
    ASSERT(!m_registry.empty(), "The example registry must not be empty.");
    auto fail = [this]()
    {
        notifyState(State::Failed);
        return 1;
    };

    auto options = parseExampleOptions(argc, argv, m_registry, defaultExample, getDeviceTypes());
    if (!options)
        return fail();

    if (options->listExamples)
    {
        for (const auto& desc : m_registry)
            std::printf("%s: %s\n", desc.name, desc.title);
        return 0;
    }

    if (!initialize())
    {
        LOG_ERROR("Failed to initialize GLFW");
        return fail();
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

#if SLANG_RHI_DEBUG
    // Global RHI configuration must be set before any worker creates a device.
    DebugLayerOptions debugLayerOptions;
    debugLayerOptions.coreValidation = true;
    getRHI()->setDebugLayerOptions(debugLayerOptions);
#endif
    int exitCode = 0;
    bool finalRoundFailed = false;
    std::vector<DeviceType> supportedTypes;
    for (DeviceType deviceType : options->deviceTypes)
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
    notifyExamplesReady();
    m_currentIndex = options->initialIndex;
    for (;;)
    {
        m_pendingIndex.reset();
        const auto& desc = m_registry[m_currentIndex];
        notifyExampleChanged();
        auto result = runExampleRound(desc, supportedTypes, options->syncFrames);
        exitCode |= result.exitCode;
        finalRoundFailed = result.exitCode != 0 || !result.anyInitialized;
        if (result.anyInitialized)
            m_lastWorkingIndex = m_currentIndex;
        if (m_closing)
            break;

        auto nextIndex = m_pendingIndex;
        if (!result.anyInitialized && !nextIndex)
        {
            // A runtime selection unsupported by every backend must not strand the user
            // without a window. An unsupported initial selection still fails normally.
            if (m_lastWorkingIndex && *m_lastWorkingIndex != m_currentIndex)
            {
                LOG_WARNING(
                    "No backend could run %s; returning to %s.",
                    desc.title,
                    m_registry[*m_lastWorkingIndex].title
                );
                nextIndex = m_lastWorkingIndex;
            }
            else
                exitCode = 1;
        }
        if (!nextIndex)
            break;
        m_currentIndex = *nextIndex;
    }
    shutdown();
    // The CLI exit code retains historical failures. A recovered example that
    // the user closes normally has stopped, even if an earlier selection failed.
    notifyState(!m_closing && finalRoundFailed ? State::Failed : State::Stopped);

    return exitCode;
}

// Run one selection across all backends, servicing main-thread tasks through shutdown.
ExampleRoundResult ExampleApp::runExampleRound(
    const ExampleDesc& desc,
    std::span<const DeviceType> deviceTypes,
    bool syncFrames
)
{
    glfwSetTime(0);
    LOG_INFO("Example: %s", desc.title);
    // Finish the main-thread registry before any worker can request a window.
    for (size_t i = 0; i < deviceTypes.size(); ++i)
        m_examples.push_back(std::make_unique<ExampleRunner>(*this));
    runExamples(desc, deviceTypes, syncFrames);

    ExampleRoundResult result;
    for (auto& example : m_examples)
    {
        result.exitCode |= example->exitCode;
        result.anyInitialized |= example->initialized;
    }
    m_examples.clear();
    m_mainExample = nullptr;
    m_picker.hide();
    return result;
}

void ExampleApp::requestExample(size_t index)
{
    if (m_closing || index >= m_registry.size())
        return;
    m_picker.hide();
    updatePickerText();
    if (index != m_currentIndex || m_pendingIndex)
        m_pendingIndex = index;
}

void ExampleApp::requestClose()
{
    m_closing = true;
    m_pendingIndex.reset();
    stopRequestedExamples();
}

void ExampleApp::stopRequestedExamples()
{
    if (!m_closing && !m_pendingIndex)
        return;
    notifyState(State::Stopping);
    for (auto& example : m_examples)
        if (!example->stopping)
            example->stop();
}

void ExampleApp::updatePickerText()
{
    // Publish snapshots; workers never read mutable picker or selection state.
    std::string text = m_picker.getText(m_registry, m_currentIndex);
    for (auto& example : m_examples)
        example->setExamplePickerText(text);
}

GLFWwindow* ExampleApp::createWindow(ExampleRunner& runner, const char* title, const char* adapterName)
{
    if (runner.stopping)
        return nullptr;
    GLFWwindow* window = createExampleWindow(title, adapterName);
    if (!window)
        return nullptr;
    runner.window = window;
    glfwSetWindowUserPointer(window, &runner);
    glfwSetWindowPosCallback(window, glfwWindowPosCallback);
    glfwSetWindowSizeCallback(window, glfwWindowSizeCallback);
    glfwSetWindowIconifyCallback(window, glfwWindowIconifyCallback);
    glfwSetWindowMaximizeCallback(window, glfwWindowMaximizeCallback);
    glfwSetFramebufferSizeCallback(window, glfwFramebufferSizeCallback);
    glfwSetWindowContentScaleCallback(window, glfwWindowContentScaleCallback);
    glfwSetCursorPosCallback(window, glfwCursorPosCallback);
    glfwSetCursorEnterCallback(window, glfwCursorEnterCallback);
    glfwSetMouseButtonCallback(window, glfwMouseButtonCallback);
    glfwSetScrollCallback(window, glfwScrollCallback);
    glfwSetKeyCallback(window, glfwKeyCallback);
    glfwSetWindowCloseCallback(window, glfwWindowCloseCallback);
    updateMainExample();
    fitWindowsToMonitor(true);
    return window;
}

void ExampleApp::destroyWindow(ExampleRunner& runner)
{
    GLFWwindow* window = runner.window;
    if (!window)
        return;
    if (m_mainExample == &runner)
        setMainExample(nullptr);
    glfwCursorEnterCallback(window, GLFW_FALSE);
    glfwSetWindowUserPointer(window, nullptr);
    // Emscripten invokes the close callback even for programmatic destruction.
    // Replacing an example must not be interpreted as a request to exit the runner.
    glfwSetWindowCloseCallback(window, nullptr);
    glfwDestroyWindow(window);
    runner.window = nullptr;
    updateMainExample();
    layoutWindows();
}

// Arrival order is nondeterministic; primary selection and layout follow the backend list.
void ExampleApp::updateMainExample()
{
    for (auto& example : m_examples)
    {
        if (example->window && !example->stopping)
        {
            setMainExample(example.get());
            return;
        }
    }
    setMainExample(nullptr);
}

void ExampleApp::broadcastEvent(ExampleRunner::Event event)
{
    for (auto& example : m_examples)
        example->postEvent(event);
}

void ExampleApp::handleKey(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    if (auto result = m_picker.handleKey(key, action, m_registry.size()); result.handled)
    {
        if (result.selection)
            requestExample(*result.selection);
        else
            updatePickerText();
        return;
    }
    for (const auto& shortcut : kExampleShortcuts)
    {
        if (key != shortcut.key)
            continue;
        if (action != GLFW_PRESS)
            return;
        switch (key)
        {
        case GLFW_KEY_F2:
            fitWindowsToMonitor();
            return;
        case GLFW_KEY_F5:
            if (hasExamplePicker() && !m_closing && !m_pendingIndex && !m_picker.open)
            {
                m_picker.show(m_currentIndex);
                updatePickerText();
            }
            return;
        case GLFW_KEY_ESCAPE:
            glfwSetWindowShouldClose(window, GLFW_TRUE);
            requestClose();
            return;
        default:
            broadcastEvent(
                {ExampleRunner::Event::Input,
                 [callback = shortcut.callback](ExampleBase& example)
                 {
                     (example.*callback)();
                     return SLANG_OK;
                 }}
            );
            return;
        }
    }
    broadcastEvent(
        {ExampleRunner::Event::Input,
         [=](ExampleBase& example)
         {
             example.onKey(key, scancode, action, mods);
             return SLANG_OK;
         }}
    );
}

void ExampleApp::glfwWindowCloseCallback(GLFWwindow* window)
{
    getWindowRunner(window).app.requestClose();
}

void ExampleApp::glfwWindowPosCallback(GLFWwindow* window, int, int)
{
    auto& app = getWindowRunner(window).app;
    if (glfwGetWindowUserPointer(window) == app.m_mainExample)
        app.layoutWindows();
}

void ExampleApp::glfwWindowSizeCallback(GLFWwindow* window, int width, int height)
{
    auto& app = getWindowRunner(window).app;
    auto* source = &getWindowRunner(window);
    source->resize();
    if (app.m_syncingWindows || source != app.m_mainExample)
        return;
    app.m_syncingWindows = true;
    for (auto& example : app.m_examples)
    {
        if (example->window && example.get() != source)
            glfwSetWindowSize(example->window, width, height);
    }
    app.m_syncingWindows = false;
    app.layoutWindows();
}

void ExampleApp::syncWindowState(GLFWwindow* window, int attribute, int value, void (*enable)(GLFWwindow*))
{
    if (m_syncingWindows)
        return;
    m_syncingWindows = true;
    for (auto& example : m_examples)
    {
        if (example->window && example->window != window && glfwGetWindowAttrib(example->window, attribute) != value)
            (value ? enable : glfwRestoreWindow)(example->window);
    }
    m_syncingWindows = false;
}

void ExampleApp::glfwWindowIconifyCallback(GLFWwindow* window, int iconified)
{
    auto& runner = getWindowRunner(window);
    runner.setIconified(iconified != 0);
    runner.app.syncWindowState(window, GLFW_ICONIFIED, iconified, glfwIconifyWindow);
}

void ExampleApp::glfwWindowMaximizeCallback(GLFWwindow* window, int maximized)
{
    getWindowRunner(window).app.syncWindowState(window, GLFW_MAXIMIZED, maximized, glfwMaximizeWindow);
}

void ExampleApp::glfwFramebufferSizeCallback(GLFWwindow* window, int, int)
{
    getWindowRunner(window).resize();
}

void ExampleApp::glfwWindowContentScaleCallback(GLFWwindow* window, float xscale, float)
{
    getWindowRunner(window).setContentScale(xscale);
}

void ExampleApp::glfwCursorPosCallback(GLFWwindow* window, double x, double y)
{
    auto& app = getWindowRunner(window).app;
    app.broadcastEvent(
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

void ExampleApp::glfwCursorEnterCallback(GLFWwindow* window, int entered)
{
    auto& app = getWindowRunner(window).app;
    app.broadcastEvent(
        {ExampleRunner::Event::Input,
         [entered](ExampleBase& example)
         {
             example.onMouseEnter(entered != 0);
             return SLANG_OK;
         }}
    );
}

void ExampleApp::glfwMouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
{
    auto& app = getWindowRunner(window).app;
    app.broadcastEvent(
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

void ExampleApp::glfwScrollCallback(GLFWwindow* window, double x, double y)
{
    auto& app = getWindowRunner(window).app;
    app.broadcastEvent(
        {ExampleRunner::Event::Input,
         [=](ExampleBase& example)
         {
             example.onScroll(float(x), float(y));
             return SLANG_OK;
         }}
    );
}

void ExampleApp::glfwKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    getWindowRunner(window).app.handleKey(window, key, scancode, action, mods);
}

int runRegisteredExamples(int argc, const char** argv, std::string_view defaultExample)
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
    ExampleApp app(registry);
    return app.run(argc, argv, defaultExample);
}

// Application scheduling and lifecycle integration.
#if SLANG_WASM

namespace {

GLFWwindow* createExampleWindow(const char* title, const char* adapterName)
{
    setBrowserAdapterName(adapterName);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
    GLFWwindow* window = glfwCreateWindow(kInitialWindowWidth, kInitialWindowHeight, title, nullptr, nullptr);
    if (window)
        updateBrowserWindowSize(window, true);
    return window;
}

} // namespace

WindowHandle ExampleApp::getWindowHandle(const ExampleRunner&)
{
    return WindowHandle::fromWGPUCanvas("#canvas");
}

void ExampleApp::updateWindowTitle(const ExampleRunner&, const char*, double fps, bool)
{
    // The HTML viewer owns the document title and displays FPS separately.
    setBrowserFps(fps);
}

std::vector<DeviceType> ExampleApp::getDeviceTypes()
{
    return {DeviceType::WGPU};
}

bool ExampleApp::initialize()
{
    // Install before GLFW registers its DOM handlers.
    initializeBrowserInput();
    if (!glfwInit())
    {
        shutdownBrowserInput();
        return false;
    }
    return true;
}

void ExampleApp::shutdown()
{
    glfwTerminate();
    shutdownBrowserInput();
}

void ExampleApp::notifyExamplesReady()
{
    publishBrowserExamples(m_registry);
}

void ExampleApp::notifyExampleChanged()
{
    const auto& desc = m_registry[m_currentIndex];
    // Examples without local shaders still need a directory for ../base imports.
    mkdir(desc.shaderSearchPath, 0777);
    notifyState(State::Loading);
    publishBrowserExample(desc, m_currentIndex);
}

void ExampleApp::notifyState(State state)
{
    static constexpr const char* names[] = {"loading", "running", "stopping", "stopped", "failed"};
    setBrowserState(names[size_t(state)]);
}

void ExampleApp::runExamples(const ExampleDesc& desc, std::span<const DeviceType> deviceTypes, bool)
{
    for (size_t i = 0; i < m_examples.size(); ++i)
    {
        auto& example = *m_examples[i];
        example.done = false;
        example.run(desc, deviceTypes[i]);
    }
    // Changes made while shutdown yielded are handled before starting another example.
    processBrowserRequests();
}

void ExampleApp::processBrowserRequests()
{
    // A close queued while no GLFW window existed takes precedence over selection.
    if (takeBrowserCloseRequest())
        requestClose();
    int index = takeBrowserRequestedExample();
    if (index >= 0)
        requestExample(size_t(index));
    stopRequestedExamples();
}

Result ExampleApp::runOnMainThread(std::function<Result()> function)
{
    return function();
}

void ExampleApp::setMainExample(ExampleRunner* example)
{
    m_mainExample = example;
}

void ExampleApp::layoutWindows(GLFWmonitor*) {}
void ExampleApp::fitWindowsToMonitor(bool) {}

#else

namespace {

GLFWwindow* createExampleWindow(const char* title, const char*)
{
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    return glfwCreateWindow(kInitialWindowWidth, kInitialWindowHeight, title, nullptr, nullptr);
}

} // namespace

WindowHandle ExampleApp::getWindowHandle(const ExampleRunner& runner)
{
    return getWindowHandleFromGLFW(runner.window);
}

void ExampleApp::updateWindowTitle(const ExampleRunner& runner, const char* title, double fps, bool vsync)
{
    char status[128];
    snprintf(status, sizeof(status), " | %.1f FPS | VSync: %s", fps, vsync ? "On" : "Off");
    std::string fullTitle = std::string(title) + status;
    glfwSetWindowTitle(runner.window, fullTitle.c_str());
}

std::vector<DeviceType> ExampleApp::getDeviceTypes()
{
    return {
        DeviceType::D3D11,
        DeviceType::D3D12,
        DeviceType::Vulkan,
        DeviceType::Metal,
        DeviceType::CPU,
        DeviceType::CUDA,
        DeviceType::WGPU
    };
}

bool ExampleApp::initialize()
{
    return glfwInit() != GLFW_FALSE;
}

void ExampleApp::shutdown()
{
    glfwTerminate();
}

void ExampleApp::notifyExamplesReady() {}
void ExampleApp::notifyExampleChanged() {}
void ExampleApp::notifyState(State) {}

void ExampleApp::runExamples(const ExampleDesc& desc, std::span<const DeviceType> deviceTypes, bool syncFrames)
{
    m_layoutMargin = 100;
    for (size_t i = 0; i < m_examples.size(); ++i)
    {
        auto& example = *m_examples[i];
        example.done = false;
        example.syncFrames = syncFrames;
        example.thread = std::thread(&ExampleRunner::run, &example, std::cref(desc), deviceTypes[i]);
    }

    // Service window requests during initialization AND shutdown. A switch only
    // asks workers to stop: joining before pumping these tasks would deadlock.
    for (;;)
    {
        // Observe close requests before queued tasks can destroy their windows.
        for (auto& example : m_examples)
            if (example->window && glfwWindowShouldClose(example->window))
                requestClose();
        processMainTasks();
        stopRequestedExamples();
        bool allDone = true;
        for (auto& example : m_examples)
            allDone &= example->done.load();
        if (allDone)
            break;
        if (syncFrames)
            startSynchronizedFrame();
        glfwWaitEventsTimeout(0.01);
        if (!m_mainExample)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    for (auto& example : m_examples)
        if (example->thread.joinable())
            example->thread.join();
}

// Synchronous handoff: the worker relinquishes access to captured state until the task finishes.
// Never wait for a worker on the main thread while it might be waiting for one of these tasks.
Result ExampleApp::runOnMainThread(std::function<Result()> function)
{
    if (std::this_thread::get_id() == m_mainThread)
        return function();
    auto task = std::make_shared<std::packaged_task<Result()>>(std::move(function));
    auto result = task->get_future();
    {
        std::lock_guard<std::mutex> lock(m_mainTaskMutex);
        m_mainTasks.push_back(
            [task]()
            {
                (*task)();
            }
        );
    }
    glfwPostEmptyEvent();
    return result.get();
}

void ExampleApp::processMainTasks()
{
    std::vector<std::function<void()>> tasks;
    {
        std::lock_guard<std::mutex> lock(m_mainTaskMutex);
        tasks.swap(m_mainTasks);
    }
    for (auto& task : tasks)
        task();
}

// Called only by the main thread, between event-pump iterations. It never waits for workers.
void ExampleApp::startSynchronizedFrame()
{
    bool hasWork = false;
    for (auto& example : m_examples)
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
    for (auto& example : m_examples)
    {
        if (!example->stopping && !example->done)
            example->startFrame(time);
    }
}

void ExampleApp::setMainExample(ExampleRunner* example)
{
    if (m_mainExample == example)
        return;
    if (m_mainExample && m_mainExample->window)
        glfwSetWindowAttrib(m_mainExample->window, GLFW_RESIZABLE, GLFW_FALSE);
    m_mainExample = example;
    if (m_mainExample)
        glfwSetWindowAttrib(m_mainExample->window, GLFW_RESIZABLE, GLFW_TRUE);
}

// Use the monitor containing most of the primary window, including on desktops
// where monitors have negative coordinates. Fall back if the window is off screen.
GLFWmonitor* ExampleApp::getExampleMonitor()
{
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    if (!m_mainExample)
        return monitor;
    int x, y, width, height;
    glfwGetWindowPos(m_mainExample->window, &x, &y);
    glfwGetWindowSize(m_mainExample->window, &width, &height);
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

void ExampleApp::layoutWindows(GLFWmonitor* monitor)
{
    // Keep a single window freely movable unless explicitly fitting it to a monitor.
    if (!m_mainExample || m_layoutInProgress || (!monitor && m_examples.size() <= 1 && m_layoutMargin != 0))
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
    for (auto& example : m_examples)
    {
        if (!example->window || example->stopping)
            continue;
        int width, height, left, top, right, bottom;
        glfwGetWindowSize(example->window, &width, &height);
        glfwGetWindowFrameSize(example->window, &left, &top, &right, &bottom);
        cellWidth = std::max(cellWidth, width + left + right);
        cellHeight = std::max(cellHeight, height + top + bottom);
    }
    m_layoutInProgress = true;
    int x = wx + m_layoutMargin;
    int y = wy + m_layoutMargin;
    for (auto& example : m_examples)
    {
        if (!example->window || example->stopping)
            continue;
        int left, top, right, bottom;
        glfwGetWindowFrameSize(example->window, &left, &top, &right, &bottom);
        if (x > wx + m_layoutMargin && x + cellWidth > wx + ww - m_layoutMargin)
        {
            x = wx + m_layoutMargin;
            y += cellHeight;
        }
        glfwSetWindowPos(example->window, x + left, y + top);
        x += cellWidth;
    }
    m_layoutInProgress = false;
}

void ExampleApp::fitWindowsToMonitor(bool initialLayout)
{
    if (!m_mainExample || m_layoutInProgress || m_syncingWindows)
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
    m_layoutInProgress = m_syncingWindows = true;
    int count = 0, frameWidth = 0, frameHeight = 0;
    for (auto& example : m_examples)
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
        glfwGetWindowSize(m_mainExample->window, &width, &height);
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
        m_layoutMargin = margin;
        for (auto& example : m_examples)
        {
            if (example->window && !example->stopping)
                glfwSetWindowSize(example->window, bestWidth, bestHeight);
        }
    }
    m_layoutInProgress = m_syncingWindows = false;
    layoutWindows(monitor);
}

#endif


} // namespace rhi::detail
