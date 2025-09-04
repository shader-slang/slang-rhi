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
#include <slang-rhi/shader-cursor.h>

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
    virtual void update() = 0;
    virtual void draw(ITexture* image) = 0;

    virtual void onResize(int width, int height, int framebufferWidth, int framebufferHeight);
    virtual void onMousePosition(float x, float y) {}
    virtual void onMouseButton(int button, int action, int mods) {}
    virtual void onScroll(float x, float y) {}

    Result createDevice(DeviceType deviceType);
    Result createWindow(uint32_t width = 640, uint32_t height = 360);
    Result createSurface(Format format = Format::Undefined);

    // Create compute pipeline

    Result _createComputeProgram(
        const char* pathOrSource,
        bool isSource,
        const char* entryPointName,
        IShaderProgram** outProgram
    );
    Result _createComputePipeline(
        const char* pathOrSource,
        bool isSource,
        const char* entryPointName,
        IComputePipeline** outPipeline
    );
    Result createComputePipeline(const char* path, const char* entryPointName, IComputePipeline** outPipeline);
    Result createComputePipelineFromSource(
        const char* source,
        const char* entryPointName,
        IComputePipeline** outPipeline
    );

    // Create render pipeline

    Result _createRenderProgram(
        const char* pathOrSource,
        bool isSource,
        const char* vertexEntryPointName,
        const char* fragmentEntryPointName,
        IShaderProgram** outProgram
    );
    Result _createRenderPipeline(
        const char* pathOrSource,
        bool isSource,
        const char* vertexEntryPointName,
        const char* fragmentEntryPointName,
        const RenderPipelineDesc& pipelineDesc,
        IRenderPipeline** outPipeline
    );
    Result createRenderPipeline(
        const char* path,
        const char* vertexEntryPointName,
        const char* fragmentEntryPointName,
        const RenderPipelineDesc& pipelineDesc,
        IRenderPipeline** outPipeline
    );
    Result createRenderPipelineFromSource(
        const char* path,
        const char* vertexEntryPointName,
        const char* fragmentEntryPointName,
        const RenderPipelineDesc& pipelineDesc,
        IRenderPipeline** outPipeline
    );

    Result blit(ITexture* dst, ITexture* src, ICommandEncoder* commandEncoder);

    void updateAndDraw(double time);

public:
    ExampleDesc m_desc;
    GLFWwindow* m_window = nullptr;
    ComPtr<IDevice> m_device;
    ComPtr<ICommandQueue> m_queue;
    ComPtr<ISurface> m_surface;

    uint32_t m_frame = 0;
    double m_time = 0.0;
    double m_timeDelta = 0.0;
    double m_frameRate = 0.0;
    float m_mousePos[2] = {0.0f, 0.0f};

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

#define PRINT_DIAGNOSTICS(diagnostics)                                                                                 \
    {                                                                                                                  \
        if (diagnostics)                                                                                               \
        {                                                                                                              \
            const char* msg = (const char*)diagnostics->getBufferPointer();                                            \
            printf("%s\n", msg);                                                                                       \
        }                                                                                                              \
    }

Result ExampleBase::_createComputeProgram(
    const char* pathOrSource,
    bool isSource,
    const char* entryPointName,
    IShaderProgram** outProgram
)
{
    ComPtr<slang::IBlob> diagnostics;
    slang::IModule* module = nullptr;
    if (isSource)
    {
        module = m_device->getSlangSession()
                     ->loadModuleFromSourceString(nullptr, nullptr, pathOrSource, diagnostics.writeRef());
    }
    else
    {
        module = m_device->getSlangSession()->loadModule(pathOrSource, diagnostics.writeRef());
    }
    PRINT_DIAGNOSTICS(diagnostics);
    if (!module)
    {
        printf("Failed to load Slang module from '%s'\n", pathOrSource);
        return SLANG_FAIL;
    }
    slang::IEntryPoint* entryPoint;
    if (!SLANG_SUCCEEDED(module->findEntryPointByName(entryPointName, &entryPoint)))
    {
        printf("Failed to find entry point '%s' in module '%s'\n", entryPointName, pathOrSource);
        return SLANG_FAIL;
    }
    ShaderProgramDesc programDesc = {};
    programDesc.linkingStyle = LinkingStyle::SingleProgram;
    slang::IComponentType* entryPoints[] = {entryPoint};
    programDesc.slangEntryPoints = entryPoints;
    programDesc.slangEntryPointCount = SLANG_COUNT_OF(entryPoints);
    programDesc.slangGlobalScope = module;
    m_device->createShaderProgram(programDesc, outProgram, diagnostics.writeRef());
    PRINT_DIAGNOSTICS(diagnostics);
    if (!(*outProgram))
    {
        printf("Failed to create program for entry point '%s' in module '%s'\n", entryPointName, pathOrSource);
        return SLANG_FAIL;
    }
    return SLANG_OK;
}

Result ExampleBase::_createComputePipeline(
    const char* pathOrSource,
    bool isSource,
    const char* entryPointName,
    IComputePipeline** outPipeline
)
{
    ComPtr<IShaderProgram> program;
    SLANG_RETURN_ON_FAIL(_createComputeProgram(pathOrSource, isSource, entryPointName, program.writeRef()));

    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = program;
    SLANG_RETURN_ON_FAIL(m_device->createComputePipeline(pipelineDesc, outPipeline));
    return SLANG_OK;
}

Result ExampleBase::createComputePipeline(const char* path, const char* entryPointName, IComputePipeline** outPipeline)
{
    return _createComputePipeline(path, false, entryPointName, outPipeline);
}

Result ExampleBase::createComputePipelineFromSource(
    const char* source,
    const char* entryPointName,
    IComputePipeline** outPipeline
)
{
    return _createComputePipeline(source, true, entryPointName, outPipeline);
}

Result ExampleBase::_createRenderProgram(
    const char* path,
    bool isSource,
    const char* vertexEntryPointName,
    const char* fragmentEntryPointName,
    IShaderProgram** outProgram
)
{
    ComPtr<slang::IBlob> diagnostics;
    slang::IModule* module = nullptr;
    if (isSource)
    {
        module =
            m_device->getSlangSession()->loadModuleFromSourceString(nullptr, nullptr, path, diagnostics.writeRef());
    }
    else
    {
        module = m_device->getSlangSession()->loadModule(path, diagnostics.writeRef());
    }
    PRINT_DIAGNOSTICS(diagnostics);
    if (!module)
    {
        printf("Failed to load Slang module from '%s'\n", path);
        return SLANG_FAIL;
    }
    slang::IEntryPoint* vertexEntryPoint;
    if (!SLANG_SUCCEEDED(module->findEntryPointByName(vertexEntryPointName, &vertexEntryPoint)))
    {
        printf("Failed to find entry point '%s' in module '%s'\n", vertexEntryPointName, path);
        return SLANG_FAIL;
    }
    slang::IEntryPoint* fragmentEntryPoint;
    if (!SLANG_SUCCEEDED(module->findEntryPointByName(fragmentEntryPointName, &fragmentEntryPoint)))
    {
        printf("Failed to find entry point '%s' in module '%s'\n", fragmentEntryPointName, path);
        return SLANG_FAIL;
    }
    ShaderProgramDesc programDesc = {};
    programDesc.linkingStyle = LinkingStyle::SingleProgram;
    slang::IComponentType* entryPoints[] = {vertexEntryPoint, fragmentEntryPoint};
    programDesc.slangEntryPoints = entryPoints;
    programDesc.slangEntryPointCount = SLANG_COUNT_OF(entryPoints);
    programDesc.slangGlobalScope = module;
    m_device->createShaderProgram(programDesc, outProgram, diagnostics.writeRef());
    PRINT_DIAGNOSTICS(diagnostics);
    if (!(*outProgram))
    {
        printf(
            "Failed to create program for entry points '%s' / '%s' in module '%s'\n",
            vertexEntryPointName,
            fragmentEntryPointName,
            path
        );
        return SLANG_FAIL;
    }
    return SLANG_OK;
}

Result ExampleBase::_createRenderPipeline(
    const char* pathOrSource,
    bool isSource,
    const char* vertexEntryPointName,
    const char* fragmentEntryPointName,
    const RenderPipelineDesc& pipelineDesc,
    IRenderPipeline** outPipeline
)
{
    ComPtr<IShaderProgram> program;
    SLANG_RETURN_ON_FAIL(
        _createRenderProgram(pathOrSource, isSource, vertexEntryPointName, fragmentEntryPointName, program.writeRef())
    );

    RenderPipelineDesc pipelineDescCopy = pipelineDesc;
    pipelineDescCopy.program = program;
    SLANG_RETURN_ON_FAIL(m_device->createRenderPipeline(pipelineDescCopy, outPipeline));
    return SLANG_OK;
}

Result ExampleBase::createRenderPipeline(
    const char* path,
    const char* vertexEntryPointName,
    const char* fragmentEntryPointName,
    const RenderPipelineDesc& pipelineDesc,
    IRenderPipeline** outPipeline
)
{
    return _createRenderPipeline(path, false, vertexEntryPointName, fragmentEntryPointName, pipelineDesc, outPipeline);
}

Result ExampleBase::createRenderPipelineFromSource(
    const char* path,
    const char* vertexEntryPointName,
    const char* fragmentEntryPointName,
    const RenderPipelineDesc& pipelineDesc,
    IRenderPipeline** outPipeline
)
{
    return _createRenderPipeline(path, true, vertexEntryPointName, fragmentEntryPointName, pipelineDesc, outPipeline);
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
                createComputePipelineFromSource(shader, "mainCompute", m_blitComputePipeline.writeRef())
            );
        }
        IComputePassEncoder* passEncoder = commandEncoder->beginComputePass();
        ShaderCursor cursor(passEncoder->bindPipeline(m_blitComputePipeline));
        cursor["dst"].setBinding(dst);
        cursor["src"].setBinding(src);
        passEncoder->dispatchCompute((width + 15) / 16, (height + 15) / 16, 1);
        passEncoder->end();
    }
}

void ExampleBase::updateAndDraw(double time)
{
    m_timeDelta = time - m_time;
    m_time = time;
    m_frameRate = 0.9 * m_frameRate + 0.1 * (m_timeDelta > 0.0 ? (1.0 / m_timeDelta) : 0.0);

    update();

    if (m_surface->getConfig())
    {
        ComPtr<ITexture> image;
        m_surface->acquireNextImage(image.writeRef());
        if (image)
        {
            draw(image.get());
            m_surface->present();
        }
    }

    m_frame += 1;
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

    {
        double time = glfwGetTime();
        for (ExampleBase* example : getExamples())
        {
            example->m_time = time;
        }
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

        double time = glfwGetTime();

        for (ExampleBase* example : getExamples())
            example->updateAndDraw(time);
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
