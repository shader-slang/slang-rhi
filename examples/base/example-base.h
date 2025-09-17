#pragma once

#include "utils.h"

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
#include <slang-rhi/shader-cursor.h>

#include "../src/core/string.h"

#include <string>
#include <vector>
#include <algorithm>

namespace rhi {

namespace example {
static void glfwWindowPosCallback(GLFWwindow* window, int xpos, int ypos);
static void glfwWindowSizeCallback(GLFWwindow* window, int width, int height);
static void glfwWindowIconifyCallback(GLFWwindow* window, int iconified);
static void glfwWindowMaximizeCallback(GLFWwindow* window, int maximized);
static void glfwFramebufferSizeCallback(GLFWwindow* window, int width, int height);
static void glfwCursorPosCallback(GLFWwindow* window, double xpos, double ypos);
static void glfwMouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
static void glfwScrollCallback(GLFWwindow* window, double xoffset, double yoffset);
static void glfwKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
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
        // DeviceType::WGPU,
    };
    std::vector<Feature> requireFeatures;
};

class ExampleBase
{
public:
    virtual ~ExampleBase();

    virtual Result init() = 0;
    virtual void shutdown() = 0;
    virtual Result update() = 0;
    virtual Result draw(ITexture* image) = 0;

    virtual void onResize(int width, int height, int framebufferWidth, int framebufferHeight);
    virtual void onMousePosition(float x, float y) {}
    virtual void onMouseButton(int button, int action, int mods) {}
    virtual void onScroll(float x, float y) {}
    virtual void onKey(int key, int scancode, int action, int mods) {}

    Result createDevice(DeviceType deviceType);
    Result createWindow(uint32_t width = 640, uint32_t height = 360);
    Result createSurface(Format format = Format::Undefined);

    Result blit(ITexture* dst, ITexture* src, ICommandEncoder* commandEncoder);

    Result updateAndDraw(double time);

public:
    ExampleDesc m_desc;
    DeviceType m_deviceType = DeviceType::Default;
    GLFWwindow* m_window = nullptr;
    ComPtr<IDevice> m_device;
    ComPtr<ICommandQueue> m_queue;
    ComPtr<ISurface> m_surface;

    uint32_t m_frame = 0;
    double m_time = 0.0;
    double m_timeDelta = 0.0;
    double m_frameRate = 0.0;
    float m_mousePos[2] = {0.0f, 0.0f};
    bool m_mouseDown[3] = {false, false, false};

private:
    ComPtr<IComputePipeline> m_blitComputePipeline;
    ComPtr<IRenderPipeline> m_blitRenderPipeline;
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
    const char* searchPaths[] = {EXAMPLE_DIR};
    deviceDesc.slang.searchPaths = searchPaths;
    deviceDesc.slang.searchPathCount = SLANG_COUNT_OF(searchPaths);
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
    glfwSetWindowPosCallback(m_window, example::glfwWindowPosCallback);
    glfwSetWindowSizeCallback(m_window, example::glfwWindowSizeCallback);
    glfwSetWindowIconifyCallback(m_window, example::glfwWindowIconifyCallback);
    glfwSetWindowMaximizeCallback(m_window, example::glfwWindowMaximizeCallback);
    glfwSetFramebufferSizeCallback(m_window, example::glfwFramebufferSizeCallback);
    glfwSetCursorPosCallback(m_window, example::glfwCursorPosCallback);
    glfwSetMouseButtonCallback(m_window, example::glfwMouseButtonCallback);
    glfwSetScrollCallback(m_window, example::glfwScrollCallback);
    glfwSetKeyCallback(m_window, example::glfwKeyCallback);

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

Result ExampleBase::blit(ITexture* dst, ITexture* src, ICommandEncoder* commandEncoder)
{
    if (!dst || !src || !commandEncoder)
    {
        return SLANG_E_INVALID_ARG;
    }

    const char* blitComputeShader = R"(
        #define DST_FORMAT "%s"
        #define DST_SRGB %s

        float linearToSrgb(float linear)
        {
            if (linear <= 0.0031308)
                return linear * 12.92;
            else
                return pow(linear, (1.0 / 2.4)) * (1.055) - 0.055;
        }

        vector<float, N> linearToSrgb<let N : int>(vector<float, N> linear)
        {
            vector<float, N> result;
            [ForceUnroll]
            for (int i = 0; i < N; ++i)
            {
                result[i] = linearToSrgb(linear[i]);
            }
            return result;
        }

        [shader("compute")]
        [numthreads(16, 16, 1)]
        void mainCompute(uint3 tid: SV_DispatchThreadID, [format(DST_FORMAT)] RWTexture2D<float4> dst, Texture2D<float4> src)
        {
            int2 size;
            src.GetDimensions(size.x, size.y);
            if (any(tid.xy >= size))
                return;
            float4 color = src[tid.xy];
            if (DST_SRGB)
                color = linearToSrgb<4>(color);
            dst[tid.xy] = color;
        }
    )";

    const char* blitRenderShader = R"(
        struct VSOut {
            float4 pos : SV_Position;
            float2 uv : UV;
        };

        [shader("vertex")]
        VSOut mainVertex(uint vid: SV_VertexID)
        {
            VSOut vsOut;
            vsOut.uv = float2((vid << 1) & 2, vid & 2);
            vsOut.pos = float4(vsOut.uv * float2(2, -2) + float2(-1, 1), 0, 1);
            return vsOut;
        }

        [shader("fragment")]
        float4 mainFragment(VSOut vsOut, Texture2D<float4> src) : SV_Target
        {
            float2 uv = vsOut.uv;
            int2 size;
            src.GetDimensions(size.x, size.y);
            int2 coord = int2(uv * size);
            return src[coord];
        }
    )";


    uint32_t width = src->getDesc().size.width;
    uint32_t height = src->getDesc().size.height;
    Format dstFormat = dst->getDesc().format;
    const FormatInfo& dstFormatInfo = getFormatInfo(dstFormat);

    if (is_set(dst->getDesc().usage, TextureUsage::RenderTarget))
    {
        if (!m_blitRenderPipeline)
        {
            RenderPipelineDesc pipelineDesc = {};
            ColorTargetDesc colorTarget = {};
            colorTarget.format = dstFormat;
            pipelineDesc.targets = &colorTarget;
            pipelineDesc.targetCount = 1;
            SLANG_RETURN_ON_FAIL(createRenderPipelineFromSource(
                m_device,
                blitRenderShader,
                "mainVertex",
                "mainFragment",
                pipelineDesc,
                m_blitRenderPipeline.writeRef()
            ));
        }
        RenderPassColorAttachment colorAttachment = {};
        colorAttachment.view = dst->getDefaultView();
        RenderPassDesc renderPassDesc = {};
        renderPassDesc.colorAttachments = &colorAttachment;
        renderPassDesc.colorAttachmentCount = 1;
        IRenderPassEncoder* passEncoder = commandEncoder->beginRenderPass(renderPassDesc);
        ShaderCursor cursor(passEncoder->bindPipeline(m_blitRenderPipeline));
        cursor["src"].setBinding(src);
        RenderState renderState = {};
        renderState.viewports[0] = Viewport::fromSize(width, height);
        renderState.viewportCount = 1;
        renderState.scissorRects[0] = ScissorRect::fromSize(width, height);
        renderState.scissorRectCount = 1;
        passEncoder->setRenderState(renderState);
        DrawArguments drawArgs = {};
        drawArgs.vertexCount = 3;
        passEncoder->draw(drawArgs);
        passEncoder->end();
    }
    else if (is_set(dst->getDesc().usage, TextureUsage::UnorderedAccess))
    {
        if (!m_blitComputePipeline)
        {
            const char* dstFormatAttribute = dstFormatInfo.slangName;
            if (dstFormat == Format::RGBA8UnormSrgb || dstFormat == Format::BGRA8UnormSrgb ||
                dstFormat == Format::BGRX8UnormSrgb)
            {
                dstFormatAttribute = "rgba8";
            }
            if (!dstFormatAttribute)
            {
                return SLANG_E_INVALID_ARG;
            }
            char shader[4096];
            snprintf(
                shader,
                sizeof(shader),
                blitComputeShader,
                dstFormatAttribute,
                dstFormatInfo.isSrgb ? "true" : "false"
            );

            SLANG_RETURN_ON_FAIL(
                createComputePipelineFromSource(m_device, shader, "mainCompute", m_blitComputePipeline.writeRef())
            );
        }
        IComputePassEncoder* passEncoder = commandEncoder->beginComputePass();
        ShaderCursor cursor(passEncoder->bindPipeline(m_blitComputePipeline));
        cursor["dst"].setBinding(dst);
        cursor["src"].setBinding(src);
        passEncoder->dispatchCompute((width + 15) / 16, (height + 15) / 16, 1);
        passEncoder->end();
    }

    return SLANG_OK;
}

Result ExampleBase::updateAndDraw(double time)
{
    m_timeDelta = time - m_time;
    m_time = time;
    m_frameRate = 0.9 * m_frameRate + 0.1 * (m_timeDelta > 0.0 ? (1.0 / m_timeDelta) : 0.0);

    SLANG_RETURN_ON_FAIL(update());

    if (m_surface->getConfig())
    {
        ComPtr<ITexture> image;
        m_surface->acquireNextImage(image.writeRef());
        if (image)
        {
            SLANG_RETURN_ON_FAIL(draw(image.get()));
            SLANG_RETURN_ON_FAIL(m_surface->present());
        }
    }

    m_frame += 1;

    return SLANG_OK;
}

namespace example {

static std::vector<ExampleBase*>& getExamples()
{
    static std::vector<ExampleBase*> examples;
    return examples;
}

static ExampleBase* mainExample;
static bool layoutInProgress = false;

static void layoutWindows()
{
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
        switch (button)
        {
        case GLFW_MOUSE_BUTTON_LEFT:
            example->m_mouseDown[0] = (action == GLFW_PRESS);
            break;
        case GLFW_MOUSE_BUTTON_MIDDLE:
            example->m_mouseDown[1] = (action == GLFW_PRESS);
            break;
        case GLFW_MOUSE_BUTTON_RIGHT:
            example->m_mouseDown[2] = (action == GLFW_PRESS);
            break;
        default:
            break;
        }
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
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    ExampleDesc desc = Example::getDesc();

    std::vector<ExampleBase*>& examples = getExamples();

    // Create an example for each supported device type
    for (DeviceType deviceType : desc.supportedDeviceTypes)
    {
        if (rhi::getRHI()->isDeviceTypeSupported(deviceType))
        {
            Example* example = new Example();
            example->m_desc = desc;
            example->m_deviceType = deviceType;
            examples.push_back(example);
        }
    }

    // Initialize device (parallel)
    parallelForEach(
        examples,
        [](ExampleBase*& example)
        {
            if (!SLANG_SUCCEEDED(example->createDevice(example->m_deviceType)))
            {
                delete example;
                example = nullptr;
            }
        }
    );
    examples.erase(std::remove(examples.begin(), examples.end(), nullptr), examples.end());

    // Create window and surface (serial due to GLFW)
    for (ExampleBase*& example : examples)
    {
        if (!SLANG_SUCCEEDED(example->createWindow()) || !SLANG_SUCCEEDED(example->createSurface()))
        {
            delete example;
            example = nullptr;
        }
    }
    examples.erase(std::remove(examples.begin(), examples.end(), nullptr), examples.end());

    // Initialize examples (parallel)
    parallelForEach(
        examples,
        [](ExampleBase*& example)
        {
            if (!SLANG_SUCCEEDED(example->init()))
            {
                delete example;
                example = nullptr;
            }
        }
    );
    examples.erase(std::remove(examples.begin(), examples.end(), nullptr), examples.end());

    if (examples.size() > 1)
    {
        mainExample = examples[0];
        layoutWindows();
    }

    {
        double time = glfwGetTime();
        for (ExampleBase* example : examples)
        {
            example->m_time = time;
        }
    }

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
            if (!SLANG_SUCCEEDED(example->updateAndDraw(time)))
            {
                // TODO: handle error
                break;
            }
        }
    }

    for (ExampleBase* example : examples)
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
