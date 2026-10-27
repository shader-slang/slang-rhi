// Triangle example: render a colored triangle using a vertex buffer, input
// layout, and graphics pipeline. Demonstrates shader loading, resource binding,
// and a draw call to the window surface.
// The overlay lists controls; F1 toggles it.

#include "../base/example.h"

namespace {

using namespace rhi;

// Define vertex structure
struct Vertex
{
    float position[3];
    float color[3];
};

// Create triangle vertex data
static const Vertex kVertexData[3] = {
    {{-0.5f, -0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}}, // Red
    {{+0.5f, -0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}}, // Green
    {{+0.0f, +0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}}, // Blue
};

// Simple example that renders a triangle.
class ExampleTriangle : public ExampleBase
{
public:
    Result init(DeviceType deviceType) override
    {
        SLANG_RETURN_ON_FAIL(
            createDevice(deviceType, {Feature::Surface, Feature::Rasterization}, {}, m_device.writeRef())
        );
        SLANG_RETURN_ON_FAIL(createWindow(m_device));
        SLANG_RETURN_ON_FAIL(createSurface(m_device, Format::Undefined, m_surface.writeRef()));
        SLANG_RETURN_ON_FAIL(initOverlay(m_device, m_surface->getConfig()->format));

        SLANG_RETURN_ON_FAIL(m_device->getQueue(QueueType::Graphics, m_queue.writeRef()));

        // Create vertex buffer
        BufferDesc vertexBufferDesc = {};
        vertexBufferDesc.size = sizeof(kVertexData);
        vertexBufferDesc.usage = BufferUsage::VertexBuffer;
        vertexBufferDesc.defaultState = ResourceState::VertexBuffer;
        vertexBufferDesc.label = "Vertex Buffer";
        SLANG_RETURN_ON_FAIL(m_device->createBuffer(vertexBufferDesc, kVertexData, m_vertexBuffer.writeRef()));

        // Create input layout
        VertexStreamDesc vertexStreams[] = {
            {sizeof(Vertex), InputSlotClass::PerVertex, 0},
        };
        InputElementDesc inputElements[] = {
            {"POSITION", 0, Format::RGB32Float, offsetof(Vertex, position), 0},
            {"COLOR", 0, Format::RGB32Float, offsetof(Vertex, color), 0},
        };
        InputLayoutDesc inputLayoutDesc = {};
        inputLayoutDesc.inputElements = inputElements;
        inputLayoutDesc.inputElementCount = SLANG_COUNT_OF(inputElements);
        inputLayoutDesc.vertexStreams = vertexStreams;
        inputLayoutDesc.vertexStreamCount = SLANG_COUNT_OF(vertexStreams);
        SLANG_RETURN_ON_FAIL(m_device->createInputLayout(inputLayoutDesc, m_inputLayout.writeRef()));

        // Create program
        ComPtr<IShaderProgram> program;
        SLANG_RETURN_ON_FAIL(
            createProgram(m_device, "triangle.slang", {"vertexMain", "fragmentMain"}, program.writeRef())
        );

        // Create render pipeline
        ColorTargetDesc colorTarget = {};
        colorTarget.format = m_surface->getInfo().preferredFormat;
        RenderPipelineDesc pipelineDesc = {};
        pipelineDesc.program = program;
        pipelineDesc.inputLayout = m_inputLayout;
        pipelineDesc.targets = &colorTarget;
        pipelineDesc.targetCount = 1;
        pipelineDesc.depthStencil.depthTestEnable = false;
        pipelineDesc.depthStencil.depthWriteEnable = false;
        SLANG_RETURN_ON_FAIL(m_device->createRenderPipeline(pipelineDesc, m_renderPipeline.writeRef()));

        return SLANG_OK;
    }

    virtual void shutdown() override
    {
        if (m_queue)
            m_queue->waitOnHost();
        m_queue.setNull();
        m_surface.setNull();
        m_device.setNull();
    }

    virtual Result update(double time) override { return SLANG_OK; }

    virtual Result draw() override
    {
        // Skip rendering if surface is not configured (eg. when window is minimized)
        if (!m_surface->getConfig())
        {
            return SLANG_OK;
        }

        // Acquire next image from the surface
        ComPtr<ITexture> image;
        m_surface->acquireNextImage(image.writeRef());
        if (!image)
        {
            return SLANG_OK;
        }

        uint32_t width = image->getDesc().size.width;
        uint32_t height = image->getDesc().size.height;

        // Start command encoding
        ComPtr<ICommandEncoder> commandEncoder;
        SLANG_RETURN_ON_FAIL(m_queue->createCommandEncoder(commandEncoder.writeRef()));

        // Setup render pass
        RenderPassColorAttachment colorAttachment = {};
        colorAttachment.view = image->getDefaultView();
        colorAttachment.loadOp = LoadOp::Clear;
        colorAttachment.clearValue[0] = m_grey;
        colorAttachment.clearValue[1] = m_grey;
        colorAttachment.clearValue[2] = m_grey;
        colorAttachment.clearValue[3] = 1.0f;

        RenderPassDesc renderPass = {};
        renderPass.colorAttachments = &colorAttachment;
        renderPass.colorAttachmentCount = 1;

        IRenderPassEncoder* passEncoder = commandEncoder->beginRenderPass(renderPass);

        // Bind pipeline
        passEncoder->bindPipeline(m_renderPipeline);

        // Set render state
        RenderState renderState = {};
        renderState.viewports[0] = Viewport::fromSize(width, height);
        renderState.viewportCount = 1;
        renderState.scissorRects[0] = ScissorRect::fromSize(width, height);
        renderState.scissorRectCount = 1;
        renderState.vertexBuffers[0].buffer = m_vertexBuffer;
        renderState.vertexBufferCount = 1;
        passEncoder->setRenderState(renderState);

        // Draw triangle
        DrawArguments drawArgs = {};
        drawArgs.vertexCount = sizeof(kVertexData) / sizeof(kVertexData[0]);
        passEncoder->draw(drawArgs);
        passEncoder->end();

        clearOverlay();
        printOverlayHeader();
        SLANG_RETURN_ON_FAIL(drawOverlay(commandEncoder, image));

        // Submit command buffer
        ComPtr<ICommandBuffer> commandBuffer;
        SLANG_RETURN_ON_FAIL(commandEncoder->finish(commandBuffer.writeRef()));
        SLANG_RETURN_ON_FAIL(m_queue->submit(commandBuffer));

        m_grey = (m_grey + (1.f / 60.f)) > 1.f ? 0.f : m_grey + (1.f / 60.f);

        // Present the surface
        return presentSurface();
    }

    virtual Result onResize(int width, int height, int framebufferWidth, int framebufferHeight) override
    {
        // Wait for GPU to be idle before resizing
        SLANG_RETURN_ON_FAIL(m_queue->waitOnHost());
        // Configure or unconfigure the surface based on the new framebuffer size
        if (framebufferWidth > 0 && framebufferHeight > 0)
        {
            SurfaceConfig surfaceConfig;
            surfaceConfig.width = framebufferWidth;
            surfaceConfig.height = framebufferHeight;
            return configureSurface(m_surface, surfaceConfig);
        }
        else
        {
            return m_surface->unconfigure();
        }
    }

public:
    ComPtr<IDevice> m_device;
    ComPtr<ISurface> m_surface;
    ComPtr<ICommandQueue> m_queue;
    ComPtr<IBuffer> m_vertexBuffer;
    ComPtr<IInputLayout> m_inputLayout;
    ComPtr<IRenderPipeline> m_renderPipeline;

    float m_grey = 0.5f;
};

} // namespace

EXAMPLE_MAIN(ExampleTriangle, "triangle", "Triangle")
