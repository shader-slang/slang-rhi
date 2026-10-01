#pragma once

#include "utils.h"

#include <slang.h>
#include <slang-rhi.h>

#if SLANG_WINDOWS_FAMILY
#define GLFW_EXPOSE_NATIVE_WIN32
#elif SLANG_LINUX_FAMILY
#define GLFW_EXPOSE_NATIVE_X11
#elif SLANG_APPLE_FAMILY
#define GLFW_EXPOSE_NATIVE_COCOA
#endif
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <slang-rhi/glfw.h>

#include <string>
#include <vector>
#include <algorithm>

namespace rhi {

// Base class for examples.
// Derive from this class to implement an example.
class ExampleBase
{
public:
    virtual ~ExampleBase();

    // Called to initialize the example for the specified device type.
    virtual Result init(DeviceType deviceType) = 0;
    // Called to shut down the example.
    virtual void shutdown() = 0;
    // Called every frame to update the example.
    virtual Result update(double time) = 0;
    // Called every frame to render the example.
    virtual Result draw() = 0;

    // Event callbacks

    // Called when the window is resized.
    virtual void onResize(int width, int height, int framebufferWidth, int framebufferHeight) {}
    // Called when the mouse is moved.
    virtual void onMousePosition(float x, float y) {}
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

    // Creates a window with the specified title and size.
    // Automatically appends device and adapter information to the title.
    Result createWindow(IDevice* device, const char* title, uint32_t width = 640, uint32_t height = 360);
    // Destroys the window.
    void destroyWindow();

    // Creates a surface for the window with the specified format.
    // Use Format::Undefined to use the preferred format.
    Result createSurface(IDevice* device, Format format, ISurface** outSurface);

public:
    DeviceType m_deviceType = DeviceType::Default;
    std::vector<Feature> m_missingFeatures;
    GLFWwindow* m_window = nullptr;

    float m_mousePos[2] = {0.0f, 0.0f};
    bool m_mouseDown[3] = {false, false, false};
};

namespace detail {
static void glfwWindowPosCallback(GLFWwindow* window, int xpos, int ypos);
static void glfwWindowSizeCallback(GLFWwindow* window, int width, int height);
static void glfwWindowIconifyCallback(GLFWwindow* window, int iconified);
static void glfwWindowMaximizeCallback(GLFWwindow* window, int maximized);
static void glfwFramebufferSizeCallback(GLFWwindow* window, int width, int height);
static void glfwCursorPosCallback(GLFWwindow* window, double xpos, double ypos);
static void glfwMouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
static void glfwScrollCallback(GLFWwindow* window, double xoffset, double yoffset);
static void glfwKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);

static std::vector<ExampleBase*>& getExamples()
{
    static std::vector<ExampleBase*> examples;
    return examples;
}

static ExampleBase* mainExample = nullptr;

} // namespace detail


ExampleBase::~ExampleBase()
{
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
        rhi::createDevice(deviceType, requiredFeatures, preprocessorMacros, outDevice, &m_missingFeatures)
    );
    m_deviceType = (*outDevice)->getDeviceType();
    return SLANG_OK;
}

Result ExampleBase::createWindow(IDevice* device, const char* title, uint32_t width, uint32_t height)
{
    const auto& deviceInfo = device->getInfo();
    char fullTitle[1024];
    snprintf(
        fullTitle,
        sizeof(fullTitle),
        "%s | %s (%s)",
        title,
        getRHI()->getDeviceTypeName(deviceInfo.deviceType),
        deviceInfo.adapterName
    );

    bool isMainExample = (this == detail::mainExample);
    glfwWindowHint(GLFW_RESIZABLE, isMainExample ? GLFW_TRUE : GLFW_FALSE);

    m_window = glfwCreateWindow(width, height, fullTitle, nullptr, nullptr);
    if (!m_window)
    {
        return SLANG_FAIL;
    }
    glfwSetWindowUserPointer(m_window, this);
    glfwSetWindowPosCallback(m_window, detail::glfwWindowPosCallback);
    glfwSetWindowSizeCallback(m_window, detail::glfwWindowSizeCallback);
    glfwSetWindowIconifyCallback(m_window, detail::glfwWindowIconifyCallback);
    glfwSetWindowMaximizeCallback(m_window, detail::glfwWindowMaximizeCallback);
    glfwSetFramebufferSizeCallback(m_window, detail::glfwFramebufferSizeCallback);
    glfwSetCursorPosCallback(m_window, detail::glfwCursorPosCallback);
    glfwSetMouseButtonCallback(m_window, detail::glfwMouseButtonCallback);
    glfwSetScrollCallback(m_window, detail::glfwScrollCallback);
    glfwSetKeyCallback(m_window, detail::glfwKeyCallback);

    return SLANG_OK;
}

void ExampleBase::destroyWindow()
{
    if (m_window)
    {
        glfwDestroyWindow(m_window);
        m_window = nullptr;
    }
}

Result ExampleBase::createSurface(IDevice* device, Format format, ISurface** outSurface)
{
    ASSERT(m_window, "Window must be created before creating surface");

    int width, height;
    glfwGetFramebufferSize(m_window, &width, &height);

    SLANG_RETURN_ON_FAIL(device->createSurface(getWindowHandleFromGLFW(m_window), outSurface));
    SurfaceConfig surfaceConfig;
    surfaceConfig.width = width;
    surfaceConfig.height = height;
    surfaceConfig.format = format;
    SLANG_RETURN_ON_FAIL((*outSurface)->configure(surfaceConfig));

    return SLANG_OK;
}

namespace detail {

static bool layoutInProgress = false;

static void layoutWindows()
{
    if (getExamples().size() <= 1)
    {
        return;
    }

    static const int kMargin = 100;

    int wx, wy, ww, wh;
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    glfwGetMonitorWorkarea(monitor, &wx, &wy, &ww, &wh);

    int frameLeft, frameTop, frameRight, frameBottom;
    glfwGetWindowFrameSize(mainExample->m_window, &frameLeft, &frameTop, &frameRight, &frameBottom);

    layoutInProgress = true;

    int x = wx + kMargin;
    int y = wy + kMargin;

    for (ExampleBase* example : getExamples())
    {
        int width, height;
        glfwGetWindowSize(example->m_window, &width, &height);
        width += frameLeft + frameRight;
        height += frameTop + frameBottom;

        if (x + width >= ww)
        {
            x = wx + kMargin;
            y += height;
        }
        glfwSetWindowPos(example->m_window, x, y);
        x += width;
    }

    layoutInProgress = false;
}

static void glfwWindowPosCallback(GLFWwindow* window, int xpos, int ypos)
{
    if (layoutInProgress)
    {
        return;
    }
    ExampleBase* srcExample = (ExampleBase*)glfwGetWindowUserPointer(window);
    if (srcExample == mainExample)
    {
        layoutWindows();
    }
}

static void glfwWindowSizeCallback(GLFWwindow* window, int width, int height)
{
    if (layoutInProgress)
    {
        return;
    }
    ExampleBase* srcExample = (ExampleBase*)glfwGetWindowUserPointer(window);
    for (ExampleBase* example : getExamples())
    {
        if (example != srcExample)
        {
            glfwSetWindowSize(example->m_window, width, height);
        }
    }
    if (srcExample == mainExample)
    {
        layoutWindows();
    }
}

static void glfwWindowIconifyCallback(GLFWwindow* window, int iconified)
{
    ExampleBase* srcExample = (ExampleBase*)glfwGetWindowUserPointer(window);
    for (ExampleBase* example : getExamples())
    {
        if (example != srcExample)
        {
            if (iconified)
            {
                glfwIconifyWindow(example->m_window);
            }
            else
            {
                glfwRestoreWindow(example->m_window);
            }
        }
    }
}

static void glfwWindowMaximizeCallback(GLFWwindow* window, int maximized)
{
    ExampleBase* srcExample = (ExampleBase*)glfwGetWindowUserPointer(window);
    for (ExampleBase* example : getExamples())
    {
        if (example != srcExample)
        {
            if (maximized)
            {
                glfwMaximizeWindow(example->m_window);
            }
            else
            {
                glfwRestoreWindow(example->m_window);
            }
        }
    }
}

static void glfwFramebufferSizeCallback(GLFWwindow* window, int width, int height)
{
    int windowWidth, windowHeight;
    glfwGetWindowSize(window, &windowWidth, &windowHeight);
    int framebufferWidth, framebufferHeight;
    glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
    ExampleBase* example = (ExampleBase*)glfwGetWindowUserPointer(window);
    example->onResize(windowWidth, windowHeight, framebufferWidth, framebufferHeight);
}

static void glfwCursorPosCallback(GLFWwindow* window, double xpos, double ypos)
{
    for (ExampleBase* example : getExamples())
    {
        example->m_mousePos[0] = xpos;
        example->m_mousePos[1] = ypos;
        example->onMousePosition(xpos, ypos);
    }
}

static void glfwMouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
{
    for (ExampleBase* example : getExamples())
    {
        if (button < SLANG_COUNT_OF(example->m_mouseDown))
            example->m_mouseDown[button] = (action == GLFW_PRESS);
        example->onMouseButton(button, action, mods);
    }
}

static void glfwScrollCallback(GLFWwindow* window, double xoffset, double yoffset)
{
    for (ExampleBase* example : getExamples())
    {
        example->onScroll(xoffset, yoffset);
    }
}

static void glfwKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    for (ExampleBase* example : getExamples())
    {
        example->onKey(key, scancode, action, mods);
        if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
        {
            glfwSetWindowShouldClose(example->m_window, GLFW_TRUE);
        }
    }
}

template<typename Example>
static int main(int argc, const char** argv)
{
    if (!glfwInit())
    {
        LOG_ERROR("Failed to initialize GLFW");
        return 1;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    std::vector<DeviceType> deviceTypes = {
        DeviceType::D3D11,
        DeviceType::D3D12,
        DeviceType::Vulkan,
        DeviceType::Metal,
        DeviceType::CPU,
        DeviceType::CUDA,
        // Exclude for now as WGPU backend is not fully functional
        // DeviceType::WGPU,
    };

    std::vector<ExampleBase*>& examples = getExamples();

    // Create an example for each supported device type
    for (DeviceType deviceType : deviceTypes)
    {
        if (rhi::getRHI()->isDeviceTypeSupported(deviceType))
        {
            Example* example = new Example();
            ExampleBase* prevMainExample = mainExample;
            if (!mainExample)
            {
                mainExample = example;
            }
            Result result = example->init(deviceType);
            if (SLANG_FAILED(result))
            {
                if (result == SLANG_E_NOT_AVAILABLE && !example->m_missingFeatures.empty())
                {
                    std::string missingFeatures;
                    for (Feature feature : example->m_missingFeatures)
                    {
                        if (!missingFeatures.empty())
                            missingFeatures += ", ";
                        const char* name = getRHI()->getFeatureName(feature);
                        missingFeatures += name ? name : "unknown";
                    }
                    LOG_INFO(
                        "%s: skipped (missing required features: %s).",
                        getRHI()->getDeviceTypeName(deviceType),
                        missingFeatures.c_str()
                    );
                }
                else
                    LOG_ERROR(
                        "Could not initialize %s example (0x%08x).",
                        getRHI()->getDeviceTypeName(deviceType),
                        unsigned(result)
                    );
                mainExample = prevMainExample;
                delete example;
                continue;
            }
            examples.push_back(example);
        }
        else
        {
            LOG_INFO("%s: skipped (backend not enabled in this build).", getRHI()->getDeviceTypeName(deviceType));
        }
    }

    layoutWindows();

    int exitCode = examples.empty() ? 1 : 0;
    if (examples.size() > 0)
    {
        while (true)
        {
            bool shouldClose = false;
            for (ExampleBase* example : examples)
            {
                if (glfwWindowShouldClose(example->m_window))
                {
                    shouldClose = true;
                    break;
                }
            }
            if (shouldClose)
            {
                break;
            }

            glfwPollEvents();

            double time = glfwGetTime();

            for (ExampleBase* example : examples)
            {
                Result result = example->update(time);
                if (SLANG_SUCCEEDED(result))
                    result = example->draw();
                if (SLANG_FAILED(result))
                {
                    LOG_ERROR(
                        "%s: example frame failed (0x%08x).",
                        getRHI()->getDeviceTypeName(example->m_deviceType),
                        unsigned(result)
                    );
                    exitCode = 1;
                    break;
                }
            }
            if (exitCode != 0)
                break;
        }

        for (ExampleBase* example : examples)
        {
            example->shutdown();
            delete example;
        }
    }

    examples.clear();
    mainExample = nullptr;

    glfwTerminate();

    return exitCode;
}

} // namespace detail

} // namespace rhi

#define EXAMPLE_MAIN(Example)                                                                                          \
    int main(int argc, const char** argv)                                                                              \
    {                                                                                                                  \
        return rhi::detail::main<Example>(argc, argv);                                                                 \
    }
