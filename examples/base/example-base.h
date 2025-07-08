#pragma once

#include <slang.h>

#if SLANG_WINDOWS_FAMILY
#define GLFW_EXPOSE_NATIVE_WIN32
#elif SLANG_LINUX_FAMILY
#define GLFW_EXPOSE_NATIVE_X11
#elif SLANG_APPLE_FAMILY
#define GLFW_EXPOSE_NATIVE_COCOA
#endif
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <slang-rhi.h>
#include <slang-rhi/glfw.h>

#include "../src/core/string.h"

#include <string>
#include <vector>

namespace rhi {

namespace example {
static void glfwWindowSizeCallback(GLFWwindow* window, int width, int height);
static void glfwWindowIconifyCallback(GLFWwindow* window, int iconified);
static void glfwWindowMaximizeCallback(GLFWwindow* window, int maximized);
static void glfwFramebufferSizeCallback(GLFWwindow* window, int width, int height);
static void glfwCursorPosCallback(GLFWwindow* window, double xpos, double ypos);
static void glfwMouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
static void glfwScrollCallback(GLFWwindow* window, double xoffset, double yoffset);
} // namespace example

class DebugPrinter : public IDebugCallback
{
public:
    virtual SLANG_NO_THROW void SLANG_MCALL handleMessage(
        DebugMessageType type,
        DebugMessageSource source,
        const char* message
    ) override
    {
        static const char* kTypeStrings[] = {"INFO", "WARN", "ERROR"};
        static const char* kSourceStrings[] = {"Layer", "Driver", "Slang"};
        printf("[%s] (%s) %s\n", kTypeStrings[int(type)], kSourceStrings[int(source)], message);
        fflush(stdout);
    }

    static DebugPrinter* getInstance()
    {
        static DebugPrinter instance;
        return &instance;
    }
};

class ExampleBase
{
public:
    float m_mousePos[2] = {0.0f, 0.0f};
    GLFWwindow* window = nullptr;
    ComPtr<IDevice> device;
    ComPtr<ISurface> surface;

    virtual ~ExampleBase();

    virtual const char* getName() const = 0;

    virtual Result init(DeviceType deviceType) = 0;
    virtual void shutdown() = 0;
    virtual void update() = 0;
    virtual void draw() = 0;

    virtual void onResize(int width, int height, int framebufferWidth, int framebufferHeight);
    virtual void onMousePosition(float x, float y) {}
    virtual void onMouseButton(int button, int action, int mods) {}
    virtual void onScroll(float x, float y) {}

    Result createDevice(DeviceType deviceType);
    Result createWindow(uint32_t width = 640, uint32_t height = 360);
    Result createSurface(Format format = Format::Undefined);
};

ExampleBase::~ExampleBase()
{
    surface.setNull();
    device.setNull();
    if (window)
    {
        glfwDestroyWindow(window);
    }
}

void ExampleBase::onResize(int width, int height, int framebufferWidth, int framebufferHeight)
{
    if (surface)
    {
        device->getQueue(QueueType::Graphics)->waitOnHost();
        if (framebufferWidth > 0 && framebufferHeight > 0)
        {
            SurfaceConfig surfaceConfig;
            surfaceConfig.width = framebufferWidth;
            surfaceConfig.height = framebufferHeight;
            surface->configure(surfaceConfig);
        }
        else
        {
            surface->unconfigure();
        }
    }
}

Result ExampleBase::createDevice(DeviceType deviceType)
{
    DeviceDesc deviceDesc = {};
    deviceDesc.deviceType = deviceType;
#ifdef _DEBUG
    getRHI()->enableDebugLayers();
    deviceDesc.enableValidation = true;
    deviceDesc.debugCallback = DebugPrinter::getInstance();
#endif
    SLANG_RETURN_ON_FAIL(getRHI()->createDevice(deviceDesc, device.writeRef()));
    return SLANG_OK;
}

Result ExampleBase::createWindow(uint32_t width, uint32_t height)
{
    const auto& deviceInfo = device->getInfo();
    std::string title = string::format(
        "%s | %s (%s)",
        getName(),
        deviceInfo.adapterName,
        getRHI()->getDeviceTypeName(deviceInfo.deviceType)
    );

    window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    if (!window)
    {
        return SLANG_FAIL;
    }
    glfwSetWindowUserPointer(window, this);
    glfwSetWindowSizeCallback(window, example::glfwWindowSizeCallback);
    glfwSetWindowIconifyCallback(window, example::glfwWindowIconifyCallback);
    glfwSetWindowMaximizeCallback(window, example::glfwWindowMaximizeCallback);
    glfwSetFramebufferSizeCallback(window, example::glfwFramebufferSizeCallback);
    glfwSetCursorPosCallback(window, example::glfwCursorPosCallback);
    glfwSetMouseButtonCallback(window, example::glfwMouseButtonCallback);
    glfwSetScrollCallback(window, example::glfwScrollCallback);

    return SLANG_OK;
}

Result ExampleBase::createSurface(Format format)
{
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);

    SLANG_RETURN_ON_FAIL(device->createSurface(getWindowHandleFromGLFW(window), surface.writeRef()));
    SurfaceConfig surfaceConfig;
    surfaceConfig.width = width;
    surfaceConfig.height = height;
    surfaceConfig.format = format;
    SLANG_RETURN_ON_FAIL(surface->configure(surfaceConfig));

    return SLANG_OK;
}

namespace example {

static std::vector<ExampleBase*>& getExamples()
{
    static std::vector<ExampleBase*> examples;
    return examples;
}

static void layoutWindows()
{
    static const int kMargin = 100;

    int wx, wy, ww, wh;
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    glfwGetMonitorWorkarea(monitor, &wx, &wy, &ww, &wh);

    int x = wx + kMargin;
    int y = wy + kMargin;
    for (ExampleBase* example : getExamples())
    {
        int width, height;
        glfwGetWindowSize(example->window, &width, &height);
        if (x + width >= ww)
        {
            x = wx + kMargin;
            y += height;
        }
        glfwSetWindowPos(example->window, x, y);
        x += width;
    }
}

static void glfwWindowSizeCallback(GLFWwindow* window, int width, int height)
{
    ExampleBase* origin = (ExampleBase*)glfwGetWindowUserPointer(window);
    for (ExampleBase* example : getExamples())
    {
        if (example != origin)
        {
            glfwSetWindowSize(example->window, width, height);
        }
    }
}

static void glfwWindowIconifyCallback(GLFWwindow* window, int iconified)
{
    ExampleBase* origin = (ExampleBase*)glfwGetWindowUserPointer(window);
    for (ExampleBase* example : getExamples())
    {
        if (example != origin)
        {
            if (iconified)
            {
                glfwIconifyWindow(example->window);
            }
            else
            {
                glfwRestoreWindow(example->window);
            }
        }
    }
}

static void glfwWindowMaximizeCallback(GLFWwindow* window, int maximized)
{
    ExampleBase* origin = (ExampleBase*)glfwGetWindowUserPointer(window);
    for (ExampleBase* example : getExamples())
    {
        if (example != origin)
        {
            if (maximized)
            {
                glfwMaximizeWindow(example->window);
            }
            else
            {
                glfwRestoreWindow(example->window);
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

template<typename Example>
static int main(int argc, const char** argv)
{
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    // std::vector<DeviceType> deviceTypes = {DeviceType::Vulkan};
    // std::vector<DeviceType> deviceTypes = {DeviceType::Vulkan, DeviceType::Metal, DeviceType::WGPU};
    std::vector<DeviceType> deviceTypes = {DeviceType::D3D11, DeviceType::D3D12, DeviceType::Vulkan, DeviceType::WGPU};
    for (DeviceType deviceType : deviceTypes)
    {
        Example* example = new Example();
        getExamples().push_back(example);
        SLANG_RETURN_ON_FAIL(example->init(deviceType));
    }

    if (getExamples().size() > 1)
    {
        layoutWindows();
    }

    while (true)
    {
        bool shouldClose = false;
        for (ExampleBase* example : getExamples())
        {
            if (glfwWindowShouldClose(example->window))
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

        for (ExampleBase* example : getExamples())
            example->update();

        for (ExampleBase* example : getExamples())
            example->draw();
    }

    for (ExampleBase* example : getExamples())
    {
        example->shutdown();
        delete example;
    }

    glfwTerminate();

    return 0;
}

} // namespace example

} // namespace rhi

#define EXAMPLE_MAIN(Example)                                                                                          \
    int main(int argc, const char** argv)                                                                              \
    {                                                                                                                  \
        return rhi::example::main<Example>(argc, argv);                                                                \
    }
