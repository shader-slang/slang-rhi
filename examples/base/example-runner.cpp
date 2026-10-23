#include "example-runner.h"

#if SLANG_WASM
#include "example-browser.h"
#include <emscripten.h>
#endif

namespace rhi::detail {

void ExampleRunner::run(const ExampleDesc& desc, DeviceType deviceType)
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
    app.notifyState(initialized && !stopping ? ExampleApp::State::Running : ExampleApp::State::Stopping);
    if (initialized && SLANG_FAILED(runFrames(*example, deviceType)))
        exitCode = 1;
    stop();
    // Final failure is published by the app after cleanup and any fallback.
    app.notifyState(ExampleApp::State::Stopping);
    example->shutdown();
    // Destroy all GPU resources on their owning thread, then destroy the window via the main thread.
    example.reset();
    complete();
}

// Input and rendering are shared; scheduling is implemented in the platform section below.
Result ExampleRunner::runFrames(ExampleBase& example, DeviceType deviceType)
{
    const char* name = getRHI()->getDeviceTypeName(deviceType);
    while (!stopping)
    {
        std::vector<Event> pending;
        double time = 0;
        if (!prepareFrame(pending, time))
            break;
        Result result = processEvents(example, pending);
        if (SLANG_FAILED(result))
        {
            LOG_ERROR("%s: example event failed (0x%08x).", name, unsigned(result));
            return result;
        }
        if (stopping)
            break;
        result = example.applyVsync();
        if (SLANG_FAILED(result))
        {
            LOG_ERROR("%s: could not apply VSync (0x%08x).", name, unsigned(result));
            return result;
        }
        bool minimized = example.m_window &&
                         (example.m_minimized || example.m_framebufferWidth == 0 || example.m_framebufferHeight == 0);
        if (minimized && !isSynchronized())
        {
            waitWhileMinimized();
            continue;
        }
        // GLFW's timer may be read from any thread; every example uses the same time origin.
        if (!isSynchronized())
            time = glfwGetTime();
        result = example.update(time);
        if (SLANG_SUCCEEDED(result) && !stopping && !minimized)
            result = example.draw();
        if (SLANG_FAILED(result))
        {
            LOG_ERROR("%s: example frame failed (0x%08x).", name, unsigned(result));
            return result;
        }
        finishFrame();
    }
    return SLANG_OK;
}

void ExampleRunner::queueEvent(Event event)
{
    if (stopping || done)
        return;
    // Coalesce only adjacent snapshots, preserving ordering with key/button transitions.
    if (!events.empty() && events.back().type == event.type &&
        (event.type == Event::Resize || event.type == Event::MousePosition))
        events.back() = std::move(event);
    else
        events.push_back(std::move(event));
}

Result ExampleRunner::processEvents(ExampleBase& example, const std::vector<Event>& pending)
{
    for (const Event& event : pending)
    {
        if (stopping)
            break;
        SLANG_RETURN_ON_FAIL(event.dispatch(example));
    }
    return SLANG_OK;
}

void ExampleRunner::resize()
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

void ExampleRunner::setContentScale(float scale)
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

void ExampleRunner::setIconified(bool value)
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

void ExampleRunner::setExamplePickerText(std::string text)
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

// Frame scheduling and event queue synchronization.
#if SLANG_WASM

bool ExampleRunner::prepareFrame(std::vector<Event>& pending, double&)
{
    glfwPollEvents();
    if (window && glfwWindowShouldClose(window))
        app.requestClose();
    app.processBrowserRequests();
    if (stopping)
        return false;
    if (window)
    {
        bool hidden = !updateBrowserWindowSize(window);
        if (hidden != iconified)
            setIconified(hidden);
    }
    pending.swap(events);
    return true;
}

void ExampleRunner::waitWhileMinimized()
{
    emscripten_sleep(16);
}

void ExampleRunner::finishFrame()
{
    // Yield until the next browser frame instead of adding a fixed delay after drawing.
    finishBrowserFrame();
}

void ExampleRunner::stop()
{
    stopping = true;
}

void ExampleRunner::complete()
{
    done = true;
}

void ExampleRunner::postEvent(Event event)
{
    queueEvent(std::move(event));
}

#else

bool ExampleRunner::prepareFrame(std::vector<Event>& pending, double& time)
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
            return false;
        framePending = false;
        pending.swap(frameEvents);
        time = frameTime;
    }
    else
        pending.swap(events);
    return true;
}

void ExampleRunner::waitWhileMinimized()
{
    std::unique_lock<std::mutex> lock(eventMutex);
    eventReady.wait(
        lock,
        [&]()
        {
            return stopping || !events.empty();
        }
    );
}

void ExampleRunner::finishFrame() {}

void ExampleRunner::stop()
{
    // Use the event mutex so a sleeping worker cannot miss this notification.
    std::lock_guard<std::mutex> lock(eventMutex);
    stopping = true;
    eventReady.notify_one();
}

void ExampleRunner::startFrame(double time)
{
    std::lock_guard<std::mutex> lock(eventMutex);
    frameEvents.swap(events);
    frameTime = time;
    frameReady = false;
    framePending = true;
    eventReady.notify_one();
}

void ExampleRunner::complete()
{
    done = true;
    glfwPostEmptyEvent();
}

void ExampleRunner::postEvent(Event event)
{
    std::lock_guard<std::mutex> lock(eventMutex);
    queueEvent(std::move(event));
    eventReady.notify_one();
}

#endif


} // namespace rhi::detail
