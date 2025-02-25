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

    virtual void initResources() = 0;
    virtual void renderFrame(ITexture* texture, uint32_t width, uint32_t height, uint32_t frameIndex) = 0;

    void init(IDevice* device)
    {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

        this->window = glfwCreateWindow(512, 512, "test-surface", nullptr, nullptr);

        this->device = device;
        this->queue = device->getQueue(QueueType::Graphics);
        REQUIRE(this->queue);
        this->surface = device->createSurface(getWindowHandleFromGLFW(window));
        REQUIRE(this->surface);

        initResources();
    }

    void shutdown()
    {
        this->surface = nullptr;

        glfwDestroyWindow(window);
    }

    void configureSurface(uint32_t width, uint32_t height)
    {
        queue->waitOnHost();

        SurfaceConfig config = {};
        config.format = surface->getInfo().preferredFormat;
        config.usage = surface->getInfo().supportedUsage;
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
            glfwPollEvents();
            ComPtr<ITexture> texture = surface->getCurrentTexture();
            CHECK(texture->getDesc().size.width == width);
            CHECK(texture->getDesc().size.height == height);
            renderFrame(texture, width, height, i);
            surface->present();
        }

        // Resize window.
        glfwSetWindowSize(window, 700, 700);
        glfwGetFramebufferSize(window, &width, &height);
        configureSurface(width, height);

        for (uint32_t i = 0; i < 10; ++i)
        {
            glfwPollEvents();
            ComPtr<ITexture> texture = surface->getCurrentTexture();
            CHECK(texture->getDesc().size.width == width);
            CHECK(texture->getDesc().size.height == height);
            renderFrame(texture, width, height, i);
            surface->present();
        }

        queue->waitOnHost();
    }
};

struct RenderSurfaceTest : SurfaceTest
{
    ComPtr<IBuffer> vertexBuffer;
    ComPtr<IRenderPipeline> pipeline;

    void initResources() override
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
        REQUIRE_CALL(loadGraphicsProgram(
            device,
            shaderProgram,
            "test-surface-render",
            "vertexMain",
            "fragmentMain",
            slangReflection
        ));

        ColorTargetDesc colorTarget = {};
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

    void renderFrame(ITexture* texture, uint32_t width, uint32_t height, uint32_t frameIndex) override
    {
        ComPtr<ITextureView> textureView = device->createTextureView(texture, {});

        auto commandEncoder = queue->createCommandEncoder();

        RenderPassColorAttachment colorAttachment = {};
        colorAttachment.view = textureView;
        colorAttachment.loadOp = LoadOp::Clear;

        RenderPassDesc renderPass = {};
        renderPass.colorAttachments = &colorAttachment;
        renderPass.colorAttachmentCount = 1;

        auto passEncoder = commandEncoder->beginRenderPass(renderPass);
        auto rootObject = passEncoder->bindPipeline(pipeline);

        RenderState renderState = {};
        renderState.viewports[0] = Viewport::fromSize(width, height);
        renderState.viewportCount = 1;
        renderState.scissorRects[0] = ScissorRect::fromSize(width, height);
        renderState.scissorRectCount = 1;
        renderState.vertexBuffers[0] = vertexBuffer;
        renderState.vertexBufferCount = 1;
        passEncoder->setRenderState(renderState);

        DrawArguments drawArgs = {};
        drawArgs.vertexCount = kVertexCount;
        passEncoder->draw(drawArgs);

        passEncoder->end();
        queue->submit(commandEncoder->finish());
    }
};

struct ComputeSurfaceTest : SurfaceTest
{
    ComPtr<ITexture> renderTexture;
    ComPtr<IComputePipeline> pipeline;

    void initResources() override
    {
        ComPtr<IShaderProgram> shaderProgram;
        slang::ProgramLayout* slangReflection;
        REQUIRE_CALL(loadComputeProgram(device, shaderProgram, "test-surface-compute", "computeMain", slangReflection));

        ComputePipelineDesc pipelineDesc = {};
        pipelineDesc.program = shaderProgram.get();
        REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));
    }

    void renderFrame(ITexture* texture, uint32_t width, uint32_t height, uint32_t frameIndex) override
    {
        bool allowUnorderedAccess = is_set(surface->getInfo().supportedUsage, TextureUsage::UnorderedAccess);

        if (!allowUnorderedAccess && (!renderTexture || renderTexture->getDesc().size.width != width ||
                                      renderTexture->getDesc().size.height != height))
        {
            TextureDesc textureDesc = {};
            textureDesc.size.width = width;
            textureDesc.size.height = height;
            textureDesc.format = surface->getConfig().format;
            textureDesc.mipLevelCount = 1;
            textureDesc.usage = TextureUsage::UnorderedAccess | TextureUsage::CopySource;
            renderTexture = device->createTexture(textureDesc);
            REQUIRE(renderTexture);
        }

        auto commandEncoder = queue->createCommandEncoder();
        auto passEncoder = commandEncoder->beginComputePass();
        auto rootObject = passEncoder->bindPipeline(pipeline);
        ShaderCursor cursor(rootObject->getEntryPoint(0));
        cursor["texture"].setBinding(allowUnorderedAccess ? texture : renderTexture);
        uint32_t dim[2] = {width, height};
        cursor["dim"].setData(dim, sizeof(dim));
        passEncoder->dispatchCompute(width, height, 1);
        passEncoder->end();
        if (!allowUnorderedAccess)
        {
            Offset3D offset = {0, 0, 0};
            Extents extents = {int32_t(width), int32_t(height), 1};
            commandEncoder->copyTexture(texture, {0, 0, 0, 0}, offset, renderTexture, {0, 0, 0, 0}, offset, extents);
        }
        queue->submit(commandEncoder->finish());
    }
};

template<typename Test>
void testSurface(IDevice* device)
{
    glfwInit();
    if (!hasMonitor())
    {
        SKIP("No monitor attached");
    }

    Test t;
    t.init(device);
    t.run();
    t.shutdown();

    glfwTerminate();
}

GPU_TEST_CASE("surface-render", D3D11 | D3D12 | Vulkan | Metal | WGPU)
{
    CHECK(device->hasFeature("surface"));
    testSurface<RenderSurfaceTest>(device);
}

// skip WGPU: RWTexture binding fails
GPU_TEST_CASE("surface-compute", D3D11 | D3D12 | Vulkan | Metal | CUDA)
{
    CHECK(device->hasFeature("surface"));
    testSurface<ComputeSurfaceTest>(device);
}
