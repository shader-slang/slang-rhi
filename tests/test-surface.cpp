#include "testing.h"

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

using namespace rhi;
using namespace rhi::testing;

struct Vertex
{
    float position[3];
    float color[3];
};

static const int kVertexCount = 3;
static const Vertex kVertexData[kVertexCount] = {
    // Triangle 1
    {{-0.5, -0.5, 0}, {1, 0, 0}},
    {{+0.5, -0.5, 0}, {0, 1, 0}},
    {{+0.0, +0.5, 0}, {0, 0, 1}},
};

static bool hasMonitor()
{
    int count;
    glfwGetMonitors(&count);
    return count > 0;
}

struct SurfaceTest
{
    ComPtr<IDevice> device;
    ComPtr<ICommandQueue> queue;

    GLFWwindow* window;
    ComPtr<ISurface> surface;

    ComPtr<IBuffer> vertexBuffer;
    ComPtr<IRenderPipeline> pipeline;

    void init(IDevice* device)
    {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

        this->window = glfwCreateWindow(500, 500, "test-surface", nullptr, nullptr);

        this->device = device;
        this->queue = device->getQueue(QueueType::Graphics);
        REQUIRE(this->queue);
        this->surface = device->createSurface(getWindowHandleFromGLFW(window));
        REQUIRE(this->surface);

        createRequiredResources();
    }

    void shutdown()
    {
        this->surface = nullptr;

        glfwDestroyWindow(window);
    }

    void createRequiredResources()
    {
        VertexStreamDesc vertexStreams[] = {
            {sizeof(Vertex), InputSlotClass::PerVertex, 0},
        };

        InputElementDesc inputElements[] = {
            // Vertex buffer data
            {"POSITION", 0, Format::R32G32B32_FLOAT, offsetof(Vertex, position), 0},
            {"COLOR", 0, Format::R32G32B32_FLOAT, offsetof(Vertex, color), 0},
        };
        InputLayoutDesc inputLayoutDesc = {};
        inputLayoutDesc.inputElementCount = SLANG_COUNT_OF(inputElements);
        inputLayoutDesc.inputElements = inputElements;
        inputLayoutDesc.vertexStreamCount = SLANG_COUNT_OF(vertexStreams);
        inputLayoutDesc.vertexStreams = vertexStreams;
        auto inputLayout = device->createInputLayout(inputLayoutDesc);
        REQUIRE(inputLayout);

        BufferDesc vertexBufferDesc;
        vertexBufferDesc.size = kVertexCount * sizeof(Vertex);
        vertexBufferDesc.usage = BufferUsage::VertexBuffer;
        vertexBufferDesc.defaultState = ResourceState::VertexBuffer;
        vertexBuffer = device->createBuffer(vertexBufferDesc, &kVertexData[0]);
        REQUIRE(vertexBuffer);

        ComPtr<IShaderProgram> shaderProgram;
        slang::ProgramLayout* slangReflection;
        REQUIRE_CALL(
            loadGraphicsProgram(device, shaderProgram, "test-surface", "vertexMain", "fragmentMain", slangReflection)
        );

        ColorTargetState colorTarget = {};
        colorTarget.format = surface->getInfo().preferredFormat;

        RenderPipelineDesc pipelineDesc = {};
        pipelineDesc.program = shaderProgram.get();
        pipelineDesc.inputLayout = inputLayout;
        pipelineDesc.depthStencil.depthTestEnable = false;
        pipelineDesc.depthStencil.depthWriteEnable = false;
        pipelineDesc.targets = &colorTarget;
        pipelineDesc.targetCount = 1;
        REQUIRE_CALL(device->createRenderPipeline(pipelineDesc, pipeline.writeRef()));
    }

    void renderFrame(uint32_t width, uint32_t height)
    {
        ComPtr<ITexture> texture = surface->getCurrentTexture();
        ComPtr<ITextureView> textureView = device->createTextureView(texture, {});

        CHECK(texture->getDesc().size.width == width);
        CHECK(texture->getDesc().size.height == height);

        auto commandEncoder = queue->createCommandEncoder();

        RenderPassColorAttachment colorAttachment = {};
        colorAttachment.view = textureView;

        RenderPassDesc renderPass = {};
        renderPass.colorAttachments = &colorAttachment;
        renderPass.colorAttachmentCount = 1;

        auto passEncoder = commandEncoder->beginRenderPass(renderPass);
        auto rootObject = passEncoder->bindPipeline(pipeline);

        RenderState renderState = {};
        renderState.viewports[0] = Viewport(width, height);
        renderState.viewportCount = 1;
        renderState.scissorRects[0] = ScissorRect(width, height);
        renderState.scissorRectCount = 1;
        renderState.vertexBuffers[0] = vertexBuffer;
        renderState.vertexBufferCount = 1;
        passEncoder->setRenderState(renderState);

        DrawArguments drawArgs = {};
        drawArgs.vertexCount = kVertexCount;
        passEncoder->draw(drawArgs);

        passEncoder->end();
        queue->submit(commandEncoder->finish());

        surface->present();
    }

    void configureSurface(uint32_t width, uint32_t height)
    {
        queue->waitOnHost();

        SurfaceConfig config = {};
        config.format = surface->getInfo().preferredFormat;
        config.width = width;
        config.height = height;
        config.vsync = false;
        REQUIRE_CALL(surface->configure(config));

        CHECK(surface->getConfig().width == width);
        CHECK(surface->getConfig().height == height);
    }

    void run()
    {
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        configureSurface(width, height);

        for (uint32_t i = 0; i < 10; ++i)
        {
            renderFrame(width, height);
        }

        // Resize window.
        glfwSetWindowSize(window, 700, 700);
        glfwGetFramebufferSize(window, &width, &height);
        configureSurface(width, height);

        for (uint32_t i = 0; i < 10; ++i)
        {
            renderFrame(width, height);
        }

        queue->waitOnHost();
    }
};

void testSurface(GpuTestContext* ctx, DeviceType deviceType)
{
    glfwInit();
    if (!hasMonitor())
    {
        SKIP("No monitor attached");
    }

    ComPtr<IDevice> device = createTestingDevice(ctx, deviceType);
    SurfaceTest t;
    t.init(device);
    t.run();
    t.shutdown();

    glfwTerminate();
}

TEST_CASE("surface")
{
    runGpuTests(
        testSurface,
        {
            DeviceType::D3D11,
            DeviceType::D3D12,
            DeviceType::Vulkan,
            DeviceType::Metal,
            DeviceType::WGPU,
        }
    );
}
