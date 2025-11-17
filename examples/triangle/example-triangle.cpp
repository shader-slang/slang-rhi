
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

#include <cstdio>

#include "example-base.h"

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

class ExampleTriangle : public ExampleBase
{
public:
    ComPtr<ICommandQueue> queue;
    ComPtr<IRenderPipeline> pipeline;
    ComPtr<IBuffer> vertexBuffer;
    ComPtr<IInputLayout> inputLayout;

    float grey = 0.5f;

    const char* getName() const override { return "Triangle-Example"; }

    Result init(DeviceType deviceType) override
    {
        createDevice(deviceType);
        createWindow();
        createSurface();

        queue = device->getQueue(QueueType::Graphics);


        // Create vertex buffer
        BufferDesc vertexBufferDesc = {};
        vertexBufferDesc.size = sizeof(kVertexData);
        vertexBufferDesc.usage = BufferUsage::VertexBuffer;
        vertexBufferDesc.defaultState = ResourceState::VertexBuffer;
        vertexBufferDesc.label = "Triangle Vertex Buffer";
        SLANG_RETURN_ON_FAIL(device->createBuffer(vertexBufferDesc, &kVertexData[0], vertexBuffer.writeRef()));

        // Create input layout
        VertexStreamDesc vertexStreams[] = {
            {sizeof(Vertex), InputSlotClass::PerVertex, 0},
        };

        InputElementDesc inputElements[] = {
            {"POSITION", 0, Format::RGB32Float, offsetof(Vertex, position), 0},
            {"COLOR", 0, Format::RGB32Float, offsetof(Vertex, color), 0},
        };

        InputLayoutDesc inputLayoutDesc = {};
        inputLayoutDesc.inputElementCount = 2;
        inputLayoutDesc.inputElements = inputElements;
        inputLayoutDesc.vertexStreamCount = 1;
        inputLayoutDesc.vertexStreams = vertexStreams;
        SLANG_RETURN_ON_FAIL(device->createInputLayout(inputLayoutDesc, inputLayout.writeRef()));

        // Load shader program
        ComPtr<IShaderProgram> shaderProgram;
        // You'll need to create a shader file - see below for shader code
        const char* shaderSource = R"(
            struct VertexInput
            {
                float3 position : POSITION;
                float3 color : COLOR;
            };

            struct VertexOutput
            {
                float4 position : SV_Position;
                float3 color : COLOR;
            };

            [shader("vertex")]
            VertexOutput vertexMain(VertexInput input)
            {
                VertexOutput output;
                output.position = float4(input.position, 1.0);
                output.color = input.color;
                return output;
            }

            [shader("fragment")]
            float4 fragmentMain(VertexOutput input) : SV_Target
            {
                return float4(input.color, 1.0);
            }
        )";

        // Get slang session from device
        ComPtr<slang::ISession> slangSession;
        SLANG_RETURN_ON_FAIL(device->getSlangSession(slangSession.writeRef()));

        // Compile shader in a module
        ComPtr<slang::IBlob> diagnosticsBlob;
        slang::IModule* slangModule = slangSession->loadModuleFromSourceString(
            "triangle-shader",       // module name
            "triangle-shader.slang", // virtual file name
            shaderSource,
            diagnosticsBlob.writeRef()
        );
        if (!slangModule)
        {
            if (diagnosticsBlob)
                fprintf(stderr, "%s\n", (const char*)diagnosticsBlob->getBufferPointer());
            return SLANG_FAIL;
        }

        ComPtr<slang::IEntryPoint> vertexEntryPoint;
        slangModule->findEntryPointByName("vertexMain", vertexEntryPoint.writeRef());
        ComPtr<slang::IEntryPoint> fragmentEntryPoint;
        slangModule->findEntryPointByName("fragmentMain", fragmentEntryPoint.writeRef());

        slang::IComponentType* componentTypes[] = {slangModule, vertexEntryPoint, fragmentEntryPoint};
        ComPtr<slang::IComponentType> composedProgram;
        slangSession->createCompositeComponentType(
            componentTypes,
            sizeof(componentTypes) / sizeof(componentTypes[0]),
            composedProgram.writeRef(),
            diagnosticsBlob.writeRef()
        );

        ShaderProgramDesc programDesc = {};
        programDesc.slangGlobalScope = composedProgram;
        SLANG_RETURN_ON_FAIL(device->createShaderProgram(programDesc, shaderProgram.writeRef()));

        // Create render pipeline
        ColorTargetDesc colorTarget = {};
        colorTarget.format = surface->getInfo().preferredFormat;

        RenderPipelineDesc pipelineDesc = {};
        pipelineDesc.program = shaderProgram;
        pipelineDesc.inputLayout = inputLayout;
        pipelineDesc.targets = &colorTarget;
        pipelineDesc.targetCount = 1;
        pipelineDesc.depthStencil.depthTestEnable = false;
        pipelineDesc.depthStencil.depthWriteEnable = false;
        SLANG_RETURN_ON_FAIL(device->createRenderPipeline(pipelineDesc, pipeline.writeRef()));

        return SLANG_OK;
    }

    virtual void shutdown() override { queue->waitOnHost(); }

    virtual void update() override {}

    virtual void draw() override
    {
        if (!surface->getConfig())
            return;

        // Wait for GPU to finish previous frame
        queue->waitOnHost();

        ComPtr<ITexture> texture;
        surface->acquireNextImage(texture.writeRef());
        if (!texture)
            return;

        ComPtr<ITextureView> view = device->createTextureView(texture, {});

        auto commandEncoder = queue->createCommandEncoder();

        // Setup render pass
        RenderPassColorAttachment colorAttachment = {};
        colorAttachment.view = view;
        colorAttachment.loadOp = LoadOp::Clear;
        colorAttachment.clearValue[0] = grey;
        colorAttachment.clearValue[1] = grey;
        colorAttachment.clearValue[2] = grey;
        colorAttachment.clearValue[3] = 1.0f;

        RenderPassDesc renderPass = {};
        renderPass.colorAttachments = &colorAttachment;
        renderPass.colorAttachmentCount = 1;

        auto passEncoder = commandEncoder->beginRenderPass(renderPass);

        // Bind pipeline
        passEncoder->bindPipeline(pipeline);

        // Set render state
        RenderState renderState = {};
        renderState.viewports[0] = Viewport::fromSize(surface->getConfig()->width, surface->getConfig()->height);
        renderState.viewportCount = 1;
        renderState.scissorRects[0] = ScissorRect::fromSize(surface->getConfig()->width, surface->getConfig()->height);
        renderState.scissorRectCount = 1;
        renderState.vertexBuffers[0].buffer = vertexBuffer;
        renderState.vertexBufferCount = 1;
        passEncoder->setRenderState(renderState);

        // Draw triangle
        DrawArguments drawArgs = {};
        drawArgs.vertexCount = sizeof(kVertexData) / sizeof(kVertexData[0]);
        passEncoder->draw(drawArgs);
        passEncoder->end();

        queue->submit(commandEncoder->finish());
        surface->present();

        grey = (grey + (1.f / 60.f)) > 1.f ? 0.f : grey + (1.f / 60.f);
    }
};

EXAMPLE_MAIN(ExampleTriangle)
