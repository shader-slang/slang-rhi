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

struct ExampleDesc
{
    std::string name;
    std::vector<DeviceType> supportedDeviceTypes = {
        DeviceType::D3D11,
        DeviceType::D3D12,
        DeviceType::Vulkan,
        DeviceType::Metal,
        DeviceType::CPU,
        DeviceType::CUDA,
        DeviceType::WGPU,
    };
    std::vector<Feature> requireFeatures;
};

class ExampleBase
{
public:
    virtual ~ExampleBase();

    virtual Result init() = 0;
    virtual void shutdown() = 0;
    virtual void update() = 0;
    virtual void draw(ITexture* image) = 0;

    virtual void onResize(int width, int height, int framebufferWidth, int framebufferHeight);
    virtual void onMousePosition(float x, float y) {}
    virtual void onMouseButton(int button, int action, int mods) {}
    virtual void onScroll(float x, float y) {}

    Result createDevice(DeviceType deviceType);
    Result createWindow(uint32_t width = 640, uint32_t height = 360);
    Result createSurface(Format format = Format::Undefined);
    void updateAndDraw();

public:
    ExampleDesc m_desc;
    float m_mousePos[2] = {0.0f, 0.0f};
    GLFWwindow* m_window = nullptr;
    ComPtr<IDevice> m_device;
    ComPtr<ICommandQueue> m_queue;
    ComPtr<ISurface> m_surface;
};

ExampleBase::~ExampleBase()
{
    m_surface.setNull();
    m_device.setNull();
    if (m_window)
    {
        glfwDestroyWindow(m_window);
    }
}

void ExampleBase::onResize(int width, int height, int framebufferWidth, int framebufferHeight)
{
    if (m_surface)
    {
        m_device->getQueue(QueueType::Graphics)->waitOnHost();
        if (framebufferWidth > 0 && framebufferHeight > 0)
        {
            SurfaceConfig surfaceConfig;
            surfaceConfig.width = framebufferWidth;
            surfaceConfig.height = framebufferHeight;
            m_surface->configure(surfaceConfig);
        }
        else
        {
            m_surface->unconfigure();
        }
    }
}

Result ExampleBase::createDevice(DeviceType deviceType)
{
    DeviceDesc deviceDesc = {};
    deviceDesc.deviceType = deviceType;
#if SLANG_RHI_DEBUG
    getRHI()->enableDebugLayers();
    deviceDesc.enableValidation = true;
    deviceDesc.debugCallback = DebugPrinter::getInstance();
#endif
    SLANG_RETURN_ON_FAIL(getRHI()->createDevice(deviceDesc, m_device.writeRef()));
    SLANG_RETURN_ON_FAIL(m_device->getQueue(QueueType::Graphics, m_queue.writeRef()));

    for (Feature feature : m_desc.requireFeatures)
    {
        if (!m_device->hasFeature(feature))
        {
            return SLANG_FAIL;
        }
    }

    return SLANG_OK;
}

Result ExampleBase::createWindow(uint32_t width, uint32_t height)
{
    const auto& deviceInfo = m_device->getInfo();
    std::string title = string::format(
        "%s | %s (%s)",
        m_desc.name,
        getRHI()->getDeviceTypeName(deviceInfo.deviceType),
        deviceInfo.adapterName
    );

    m_window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    if (!m_window)
    {
        return SLANG_FAIL;
    }
    glfwSetWindowUserPointer(m_window, this);
    glfwSetWindowSizeCallback(m_window, example::glfwWindowSizeCallback);
    glfwSetWindowIconifyCallback(m_window, example::glfwWindowIconifyCallback);
    glfwSetWindowMaximizeCallback(m_window, example::glfwWindowMaximizeCallback);
    glfwSetFramebufferSizeCallback(m_window, example::glfwFramebufferSizeCallback);
    glfwSetCursorPosCallback(m_window, example::glfwCursorPosCallback);
    glfwSetMouseButtonCallback(m_window, example::glfwMouseButtonCallback);
    glfwSetScrollCallback(m_window, example::glfwScrollCallback);

    return SLANG_OK;
}

Result ExampleBase::createSurface(Format format)
{
    int width, height;
    glfwGetFramebufferSize(m_window, &width, &height);

    SLANG_RETURN_ON_FAIL(m_device->createSurface(getWindowHandleFromGLFW(m_window), m_surface.writeRef()));
    SurfaceConfig surfaceConfig;
    surfaceConfig.width = width;
    surfaceConfig.height = height;
    surfaceConfig.format = format;
    SLANG_RETURN_ON_FAIL(m_surface->configure(surfaceConfig));

    return SLANG_OK;
}

void ExampleBase::updateAndDraw()
{
    update();

    if (!m_surface->getConfig())
        return;

    ComPtr<ITexture> image;
    m_surface->acquireNextImage(image.writeRef());
    if (!image)
    {
        return;
    }

    draw(image.get());

    m_surface->present();
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
        glfwGetWindowSize(example->m_window, &width, &height);
        if (x + width >= ww)
        {
            x = wx + kMargin;
            y += height;
        }
        glfwSetWindowPos(example->m_window, x, y);
        x += width;
    }
}

static void glfwWindowSizeCallback(GLFWwindow* window, int width, int height)
{
    ExampleBase* srcExample = (ExampleBase*)glfwGetWindowUserPointer(window);
    for (ExampleBase* example : getExamples())
    {
        if (example != srcExample)
        {
            glfwSetWindowSize(example->m_window, width, height);
        }
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

    ExampleDesc desc = Example::getDesc();

    for (DeviceType deviceType : desc.supportedDeviceTypes)
    {
        if (!rhi::getRHI()->isDeviceTypeSupported(deviceType))
        {
            continue;
        }

        Example* example = new Example();
        example->m_desc = desc;

        if (!SLANG_SUCCEEDED(example->createDevice(deviceType)))
        {
            delete example;
            continue;
        }

        if (!SLANG_SUCCEEDED(example->createWindow()))
        {
            delete example;
            continue;
        }

        if (!SLANG_SUCCEEDED(example->createSurface()))
        {
            delete example;
            continue;
        }

        getExamples().push_back(example);
        SLANG_RETURN_ON_FAIL(example->init());
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

        for (ExampleBase* example : getExamples())
            example->updateAndDraw();
    }

    for (ExampleBase* example : getExamples())
    {
        example->m_queue->waitOnHost();
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
