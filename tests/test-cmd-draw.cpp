#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

struct Vertex
{
    float position[3];
};

struct Instance
{
    float position[3];
    float color[3];
};

static const int kVertexCount = 6;
static const Vertex kVertexData[kVertexCount] = {
    // Triangle 1
    {0, 0, 0.5},
    {1, 0, 0.5},
    {0, 1, 0.5},

    // Triangle 2
    {-1, 0, 0.5},
    {0, 0, 0.5},
    {-1, 1, 0.5},
};

static const int kInstanceCount = 2;
static const Instance kInstanceData[kInstanceCount] = {
    {{0, 0, 0}, {1, 0, 0}},
    {{0, -1, 0}, {0, 0, 1}},
};

static const int kIndexCount = 6;
static const uint32_t kIndexData[kIndexCount] = {
    0,
    2,
    5,
    0,
    1,
    2,
};

const int kWidth = 256;
const int kHeight = 256;
const Format format = Format::RGBA32Float;

static ComPtr<IBuffer> createVertexBuffer(IDevice* device)
{
    BufferDesc vertexBufferDesc;
    vertexBufferDesc.size = kVertexCount * sizeof(Vertex);
    vertexBufferDesc.usage = BufferUsage::VertexBuffer;
    vertexBufferDesc.defaultState = ResourceState::VertexBuffer;
    ComPtr<IBuffer> vertexBuffer = device->createBuffer(vertexBufferDesc, &kVertexData[0]);
    REQUIRE(vertexBuffer != nullptr);
    return vertexBuffer;
}

static ComPtr<IBuffer> createInstanceBuffer(IDevice* device)
{
    BufferDesc instanceBufferDesc;
    instanceBufferDesc.size = kInstanceCount * sizeof(Instance);
    instanceBufferDesc.usage = BufferUsage::VertexBuffer;
    instanceBufferDesc.defaultState = ResourceState::VertexBuffer;
    ComPtr<IBuffer> instanceBuffer = device->createBuffer(instanceBufferDesc, &kInstanceData[0]);
    REQUIRE(instanceBuffer != nullptr);
    return instanceBuffer;
}

static ComPtr<IBuffer> createIndexBuffer(IDevice* device)
{
    BufferDesc indexBufferDesc;
    indexBufferDesc.size = kIndexCount * sizeof(uint32_t);
    indexBufferDesc.usage = BufferUsage::IndexBuffer;
    indexBufferDesc.defaultState = ResourceState::IndexBuffer;
    ComPtr<IBuffer> indexBuffer = device->createBuffer(indexBufferDesc, &kIndexData[0]);
    REQUIRE(indexBuffer != nullptr);
    return indexBuffer;
}

static ComPtr<ITexture> createColorBuffer(IDevice* device)
{
    TextureDesc colorBufferDesc;
    colorBufferDesc.type = TextureType::Texture2D;
    colorBufferDesc.size.width = kWidth;
    colorBufferDesc.size.height = kHeight;
    colorBufferDesc.size.depth = 1;
    colorBufferDesc.mipCount = 1;
    colorBufferDesc.format = format;
    colorBufferDesc.usage = TextureUsage::RenderTarget | TextureUsage::CopySource;
    colorBufferDesc.defaultState = ResourceState::RenderTarget;
    ComPtr<ITexture> colorBuffer = device->createTexture(colorBufferDesc, nullptr);
    REQUIRE(colorBuffer != nullptr);
    return colorBuffer;
}

class BaseDrawTest
{
public:
    ComPtr<IDevice> device;

    ComPtr<IRenderPipeline> pipeline;

    ComPtr<IBuffer> vertexBuffer;
    ComPtr<IBuffer> instanceBuffer;
    ComPtr<ITexture> colorBuffer;
    ComPtr<ITextureView> colorBufferView;

    void init(IDevice* device_) { this->device = device_; }

    void createRequiredResources()
    {
        VertexStreamDesc vertexStreams[] = {
            {sizeof(Vertex), InputSlotClass::PerVertex, 0},
            {sizeof(Instance), InputSlotClass::PerInstance, 1},
        };

        InputElementDesc inputElements[] = {
            // Vertex buffer data
            {"POSITIONA", 0, Format::RGB32Float, offsetof(Vertex, position), 0},

            // Instance buffer data
            {"POSITIONB", 0, Format::RGB32Float, offsetof(Instance, position), 1},
            {"COLOR", 0, Format::RGB32Float, offsetof(Instance, color), 1},
        };
        InputLayoutDesc inputLayoutDesc = {};
        inputLayoutDesc.inputElementCount = SLANG_COUNT_OF(inputElements);
        inputLayoutDesc.inputElements = inputElements;
        inputLayoutDesc.vertexStreamCount = SLANG_COUNT_OF(vertexStreams);
        inputLayoutDesc.vertexStreams = vertexStreams;
        auto inputLayout = device->createInputLayout(inputLayoutDesc);
        REQUIRE(inputLayout != nullptr);

        vertexBuffer = createVertexBuffer(device);
        instanceBuffer = createInstanceBuffer(device);
        colorBuffer = createColorBuffer(device);

        ComPtr<IShaderProgram> shaderProgram;
        REQUIRE_CALL(loadProgram(device, "test-cmd-draw", {"vertexMain", "fragmentMain"}, shaderProgram.writeRef()));

        ColorTargetDesc colorTarget;
        colorTarget.format = format;
        RenderPipelineDesc pipelineDesc = {};
        pipelineDesc.program = shaderProgram.get();
        pipelineDesc.inputLayout = inputLayout;
        pipelineDesc.targets = &colorTarget;
        pipelineDesc.targetCount = 1;
        pipelineDesc.depthStencil.depthTestEnable = false;
        pipelineDesc.depthStencil.depthWriteEnable = false;
        REQUIRE_CALL(device->createRenderPipeline(pipelineDesc, pipeline.writeRef()));

        TextureViewDesc colorBufferViewDesc = {};
        colorBufferViewDesc.format = format;
        REQUIRE_CALL(device->createTextureView(colorBuffer, colorBufferViewDesc, colorBufferView.writeRef()));
    }

    void checkTestResults(
        int pixelCount,
        int channelCount,
        const int* testXCoords,
        const int* testYCoords,
        float* testResults
    )
    {
        // Read texture values back from four specific pixels located within the triangles
        // and compare against expected values (because testing every single pixel will be too long and tedious
        // and requires maintaining reference images).
        ComPtr<ISlangBlob> resultBlob;
        SubresourceLayout layout;
        REQUIRE_CALL(device->readTexture(colorBuffer, 0, 0, resultBlob.writeRef(), &layout));
        auto result = (float*)resultBlob->getBufferPointer();

        int cursor = 0;
        for (int i = 0; i < pixelCount; ++i)
        {
            auto x = testXCoords[i];
            auto y = testYCoords[i];
            auto pixelPtr = result + x * channelCount + y * layout.rowPitch / sizeof(float);
            for (int j = 0; j < channelCount; ++j)
            {
                testResults[cursor] = pixelPtr[j];
                cursor++;
            }
        }

        float expectedResult[] =
            {1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f};
        compareResultFuzzy(testResults, expectedResult, SLANG_COUNT_OF(expectedResult));
    }
};

struct DrawInstancedTest : BaseDrawTest
{
    void setUpAndDraw()
    {
        createRequiredResources();

        auto queue = device->getQueue(QueueType::Graphics);
        auto commandEncoder = queue->createCommandEncoder();

        RenderPassColorAttachment colorAttachment;
        colorAttachment.view = colorBufferView;
        colorAttachment.loadOp = LoadOp::Clear;
        colorAttachment.storeOp = StoreOp::Store;
        RenderPassDesc renderPass;
        renderPass.colorAttachments = &colorAttachment;
        renderPass.colorAttachmentCount = 1;
        auto passEncoder = commandEncoder->beginRenderPass(renderPass);

        passEncoder->bindPipeline(pipeline);

        RenderState state;
        state.viewports[0] = Viewport::fromSize(kWidth, kHeight);
        state.viewportCount = 1;
        state.scissorRects[0] = ScissorRect::fromSize(kWidth, kHeight);
        state.scissorRectCount = 1;
        state.vertexBuffers[0] = vertexBuffer;
        state.vertexBuffers[1] = instanceBuffer;
        state.vertexBufferCount = 2;
        passEncoder->setRenderState(state);

        DrawArguments args;
        args.vertexCount = 3;
        args.instanceCount = kInstanceCount;
        passEncoder->draw(args);
        // The second triangle starts at vertex three. Direct draws must use startVertexLocation,
        // independently of the index offset used by indexed draws.
        args.startVertexLocation = 3;
        passEncoder->draw(args);
        passEncoder->end();

        queue->submit(commandEncoder->finish());
        queue->waitOnHost();
    }

    void run()
    {
        setUpAndDraw();

        const int kPixelCount = 4;
        const int kChannelCount = 4;
        int testXCoords[kPixelCount] = {64, 192, 64, 192};
        int testYCoords[kPixelCount] = {100, 100, 250, 250};
        float testResults[kPixelCount * kChannelCount];

        checkTestResults(kPixelCount, kChannelCount, testXCoords, testYCoords, testResults);
    }
};

struct DrawIndexedInstancedTest : BaseDrawTest
{
    ComPtr<IBuffer> indexBuffer;

    void setUpAndDraw()
    {
        createRequiredResources();

        auto queue = device->getQueue(QueueType::Graphics);
        auto commandEncoder = queue->createCommandEncoder();

        RenderPassColorAttachment colorAttachment;
        colorAttachment.view = colorBufferView;
        colorAttachment.loadOp = LoadOp::Clear;
        colorAttachment.storeOp = StoreOp::Store;
        RenderPassDesc renderPass;
        renderPass.colorAttachments = &colorAttachment;
        renderPass.colorAttachmentCount = 1;
        auto passEncoder = commandEncoder->beginRenderPass(renderPass);

        passEncoder->bindPipeline(pipeline);

        RenderState state;
        state.vertexBuffers[0] = vertexBuffer;
        state.vertexBuffers[1] = instanceBuffer;
        state.vertexBufferCount = 2;
        state.indexBuffer = indexBuffer;
        state.indexFormat = IndexFormat::Uint32;
        state.viewports[0] = Viewport::fromSize(kWidth, kHeight);
        state.viewportCount = 1;
        state.scissorRects[0] = ScissorRect::fromSize(kWidth, kHeight);
        state.scissorRectCount = 1;
        passEncoder->setRenderState(state);

        DrawArguments args;
        args.vertexCount = kVertexCount;
        args.instanceCount = kInstanceCount;
        passEncoder->drawIndexed(args);
        passEncoder->end();

        queue->submit(commandEncoder->finish());
        queue->waitOnHost();
    }

    void run()
    {
        indexBuffer = createIndexBuffer(device);

        setUpAndDraw();

        const int kPixelCount = 4;
        const int kChannelCount = 4;
        int testXCoords[kPixelCount] = {64, 192, 64, 192};
        int testYCoords[kPixelCount] = {32, 100, 150, 250};
        float testResults[kPixelCount * kChannelCount];

        checkTestResults(kPixelCount, kChannelCount, testXCoords, testYCoords, testResults);
    }
};

struct DrawIndirectTest : BaseDrawTest
{
    ComPtr<IBuffer> indirectBuffer;

    struct IndirectArgData
    {
        float padding; // Ensure args and count don't start at 0 offset for testing purposes
        IndirectDrawArguments args;
    };

    ComPtr<IBuffer> createIndirectBuffer()
    {
        static const IndirectArgData kIndirectData = {
            42.0f,        // padding
            {6, 2, 0, 0}, // args
        };

        BufferDesc bufferDesc;
        bufferDesc.size = sizeof(IndirectArgData);
        bufferDesc.usage = BufferUsage::IndirectArgument;
        bufferDesc.defaultState = ResourceState::IndirectArgument;
        ComPtr<IBuffer> buffer = device->createBuffer(bufferDesc, &kIndirectData);
        REQUIRE(buffer != nullptr);
        return buffer;
    }

    void setUpAndDraw()
    {
        createRequiredResources();

        auto queue = device->getQueue(QueueType::Graphics);
        auto commandEncoder = queue->createCommandEncoder();

        RenderPassColorAttachment colorAttachment;
        colorAttachment.view = colorBufferView;
        colorAttachment.loadOp = LoadOp::Clear;
        colorAttachment.storeOp = StoreOp::Store;
        RenderPassDesc renderPass;
        renderPass.colorAttachments = &colorAttachment;
        renderPass.colorAttachmentCount = 1;
        auto passEncoder = commandEncoder->beginRenderPass(renderPass);

        passEncoder->bindPipeline(pipeline);

        RenderState state;
        state.vertexBuffers[0] = vertexBuffer;
        state.vertexBuffers[1] = instanceBuffer;
        state.vertexBufferCount = 2;
        state.viewports[0] = Viewport::fromSize(kWidth, kHeight);
        state.viewportCount = 1;
        state.scissorRects[0] = ScissorRect::fromSize(kWidth, kHeight);
        state.scissorRectCount = 1;
        passEncoder->setRenderState(state);

        uint32_t maxDrawCount = 1;
        BufferOffsetPair argBuffer = {indirectBuffer, offsetof(IndirectArgData, args)};
        passEncoder->drawIndirect(maxDrawCount, argBuffer);
        passEncoder->end();

        queue->submit(commandEncoder->finish());
        queue->waitOnHost();
    }

    void run()
    {
        indirectBuffer = createIndirectBuffer();

        setUpAndDraw();

        const int kPixelCount = 4;
        const int kChannelCount = 4;
        int testXCoords[kPixelCount] = {64, 192, 64, 192};
        int testYCoords[kPixelCount] = {100, 100, 250, 250};
        float testResults[kPixelCount * kChannelCount];

        checkTestResults(kPixelCount, kChannelCount, testXCoords, testYCoords, testResults);
    }
};

struct DrawIndexedIndirectTest : BaseDrawTest
{
    ComPtr<IBuffer> indexBuffer;
    ComPtr<IBuffer> indirectBuffer;

    struct IndexedIndirectArgData
    {
        float padding; // Ensure args and count don't start at 0 offset for testing purposes
        IndirectDrawIndexedArguments args;
    };

    ComPtr<IBuffer> createIndirectBuffer()
    {
        static const IndexedIndirectArgData kIndexedIndirectData = {
            42.0f,           // padding
            {6, 2, 0, 0, 0}, // args
        };

        BufferDesc bufferDesc;
        bufferDesc.size = sizeof(IndexedIndirectArgData);
        bufferDesc.usage = BufferUsage::IndirectArgument;
        bufferDesc.defaultState = ResourceState::IndirectArgument;
        ComPtr<IBuffer> buffer = device->createBuffer(bufferDesc, &kIndexedIndirectData);
        REQUIRE(buffer != nullptr);
        return buffer;
    }

    void setUpAndDraw()
    {
        createRequiredResources();

        auto queue = device->getQueue(QueueType::Graphics);
        auto commandEncoder = queue->createCommandEncoder();

        RenderPassColorAttachment colorAttachment;
        colorAttachment.view = colorBufferView;
        colorAttachment.loadOp = LoadOp::Clear;
        colorAttachment.storeOp = StoreOp::Store;
        RenderPassDesc renderPass;
        renderPass.colorAttachments = &colorAttachment;
        renderPass.colorAttachmentCount = 1;
        auto passEncoder = commandEncoder->beginRenderPass(renderPass);

        passEncoder->bindPipeline(pipeline);

        RenderState state;
        state.vertexBuffers[0] = vertexBuffer;
        state.vertexBuffers[1] = instanceBuffer;
        state.vertexBufferCount = 2;
        state.indexBuffer = indexBuffer;
        state.indexFormat = IndexFormat::Uint32;
        state.viewports[0] = Viewport::fromSize(kWidth, kHeight);
        state.viewportCount = 1;
        state.scissorRects[0] = ScissorRect::fromSize(kWidth, kHeight);
        state.scissorRectCount = 1;
        passEncoder->setRenderState(state);

        uint32_t maxDrawCount = 1;
        BufferOffsetPair argBuffer = {indirectBuffer, offsetof(IndexedIndirectArgData, args)};
        passEncoder->drawIndexedIndirect(maxDrawCount, argBuffer);
        passEncoder->end();

        queue->submit(commandEncoder->finish());
        queue->waitOnHost();
    }

    void run()
    {
        indexBuffer = createIndexBuffer(device);
        indirectBuffer = createIndirectBuffer();

        setUpAndDraw();

        const int kPixelCount = 4;
        const int kChannelCount = 4;
        int testXCoords[kPixelCount] = {64, 192, 64, 192};
        int testYCoords[kPixelCount] = {32, 100, 150, 250};
        float testResults[kPixelCount * kChannelCount];

        checkTestResults(kPixelCount, kChannelCount, testXCoords, testYCoords, testResults);
    }
};

template<typename T>
void testDraw(IDevice* device)
{
    T test;
    test.init(device);
    test.run();
}

GPU_TEST_CASE("cmd-draw-instanced", D3D11 | D3D12 | Vulkan | Metal | WGPU)
{
    testDraw<DrawInstancedTest>(device);
}

GPU_TEST_CASE("cmd-draw-indexed-instanced", D3D11 | D3D12 | Vulkan | Metal | WGPU)
{
    testDraw<DrawIndexedInstancedTest>(device);
}

GPU_TEST_CASE("cmd-draw-indirect", D3D11 | D3D12 | Vulkan)
{
    testDraw<DrawIndirectTest>(device);
}

GPU_TEST_CASE("cmd-draw-indexed-indirect", D3D11 | D3D12 | Vulkan)
{
    testDraw<DrawIndexedIndirectTest>(device);
}

GPU_TEST_CASE("cmd-draw-no-input-layout", D3D11 | D3D12 | Vulkan | Metal | WGPU)
{
    ComPtr<IShaderProgram> program;
    REQUIRE_CALL(loadProgram(
        device,
        "test-parallel-pipeline-creation-render",
        {"vertexMain", "fragmentMain"},
        program.writeRef()
    ));

    TextureDesc textureDesc = {};
    textureDesc.type = TextureType::Texture2D;
    textureDesc.size = {4, 4, 1};
    textureDesc.format = Format::RGBA8Unorm;
    textureDesc.usage = TextureUsage::RenderTarget | TextureUsage::CopySource;
    textureDesc.defaultState = ResourceState::RenderTarget;
    auto texture = device->createTexture(textureDesc, nullptr);
    REQUIRE(texture);
    auto view = device->createTextureView(texture, {});
    REQUIRE(view);

    // A null layout and an explicitly empty layout both describe a shader that
    // generates its vertices from SV_VertexID without any vertex buffers.
    for (bool explicitEmptyLayout : {false, true})
    {
        CAPTURE(explicitEmptyLayout);
        ComPtr<IInputLayout> layout;
        if (explicitEmptyLayout)
            REQUIRE_CALL(device->createInputLayout({}, layout.writeRef()));

        ColorTargetDesc target = {};
        target.format = textureDesc.format;
        RenderPipelineDesc pipelineDesc = {};
        pipelineDesc.program = program;
        pipelineDesc.inputLayout = layout;
        pipelineDesc.targets = &target;
        pipelineDesc.targetCount = 1;
        pipelineDesc.depthStencil.depthTestEnable = false;
        pipelineDesc.depthStencil.depthWriteEnable = false;
        pipelineDesc.compilationPolicy = PipelineCompilationPolicy::Immediate;
        auto pipeline = device->createRenderPipeline(pipelineDesc);
        REQUIRE(pipeline);

        auto queue = device->getQueue(QueueType::Graphics);
        auto encoder = queue->createCommandEncoder();
        RenderPassColorAttachment attachment = {};
        attachment.view = view;
        attachment.loadOp = LoadOp::Clear;
        attachment.storeOp = StoreOp::Store;
        RenderPassDesc passDesc = {};
        passDesc.colorAttachments = &attachment;
        passDesc.colorAttachmentCount = 1;
        auto pass = encoder->beginRenderPass(passDesc);
        pass->bindPipeline(pipeline);
        RenderState state = {};
        state.viewports[0] = Viewport::fromSize(4, 4);
        state.viewportCount = 1;
        state.scissorRects[0] = ScissorRect::fromSize(4, 4);
        state.scissorRectCount = 1;
        pass->setRenderState(state);
        DrawArguments args = {};
        args.vertexCount = 3;
        pass->draw(args);
        pass->end();
        ComPtr<ICommandBuffer> commands;
        REQUIRE_CALL(encoder->finish(commands.writeRef()));
        REQUIRE_CALL(queue->submit(commands));
        REQUIRE_CALL(queue->waitOnHost());

        std::array<uint8_t, 4 * 4 * 4> expected = {};
        for (size_t i = 0; i < expected.size(); i += 4)
        {
            expected[i] = 255;
            expected[i + 3] = 255;
        }
        compareComputeResult(device, texture, 0, 0, expected);
    }
}

GPU_TEST_CASE("cmd-draw-entry-point-layout-groups", Vulkan)
{
    auto session = device->getSlangSession();
    ComPtr<ISlangBlob> diagnostics;
    auto module = session->loadModuleFromSourceString(
        "drawLayoutGroups",
        "drawLayoutGroups.slang",
        R"(
        [shader("vertex")]
        float4 vertexMain(uint id : SV_VertexID, uniform float4x4 transform) : SV_Position
        {
            float2 p = id == 0 ? float2(-1, -1) : id == 1 ? float2(3, -1) : float2(-1, 3);
            return float4(p * transform[0].xy, 0.5, 1);
        }
        [shader("fragment")]
        float4 fragmentA(uniform float4 color) : SV_Target { return color; }
        [shader("fragment")]
        float4 fragmentB(uniform float4 color, uniform float4 extra) : SV_Target { return color + extra; }
    )",
        diagnostics.writeRef()
    );
    REQUIRE(module);
    ComPtr<slang::IEntryPoint> vertex, fragmentA, fragmentB;
    REQUIRE_CALL(module->findEntryPointByName("vertexMain", vertex.writeRef()));
    REQUIRE_CALL(module->findEntryPointByName("fragmentA", fragmentA.writeRef()));
    REQUIRE_CALL(module->findEntryPointByName("fragmentB", fragmentB.writeRef()));
    slang::IComponentType* parts[] = {module, vertex, fragmentA, fragmentB};
    ComPtr<slang::IComponentType> shaders, constraints, batch, linked;
    REQUIRE_CALL(session->createCompositeComponentType(parts, 4, shaders.writeRef(), diagnostics.writeRef()));
    ComPtr<slang::IEntryPointUniformLayoutService_Experimental> service;
    REQUIRE_CALL(shaders->queryInterface(SLANG_IID_PPV_ARGS(service.writeRef())));
    SlangInt first[] = {0, 1}, second[] = {0, 2};
    slang::EntryPointUniformLayoutGroup groups[] = {{first, 2}, {second, 2}};
    REQUIRE_CALL(
        service->createEntryPointUniformLayoutConstraints(groups, 2, constraints.writeRef(), diagnostics.writeRef())
    );
    slang::IComponentType* batchParts[] = {shaders, constraints};
    REQUIRE_CALL(session->createCompositeComponentType(batchParts, 2, batch.writeRef(), diagnostics.writeRef()));
    REQUIRE_CALL(batch->link(linked.writeRef(), diagnostics.writeRef()));
    auto reflection = linked->getLayout();
    REQUIRE(reflection);
    CHECK(reflection->getEntryPointByIndex(1)->getPushConstantOffset() == 64);
    CHECK(reflection->getEntryPointByIndex(2)->getPushConstantOffset() == 64);

    ComPtr<IRenderPipeline> pipelines[2];
    for (uint32_t group = 0; group < 2; ++group)
    {
        ShaderProgramDesc programDesc;
        programDesc.slangGlobalScope = linked;
        programDesc.entryPointUniformLayoutGroupIndex = group;
        uint32_t selected[] = {group + 1, 0};
        if (group == 1)
        {
            programDesc.selectedEntryPointIndices = selected;
            programDesc.selectedEntryPointCount = 2;
        }
        auto program = device->createShaderProgram(programDesc);
        REQUIRE(program);
        ColorTargetDesc target;
        target.format = Format::RGBA32Float;
        RenderPipelineDesc pipelineDesc;
        pipelineDesc.program = program;
        pipelineDesc.targets = &target;
        pipelineDesc.targetCount = 1;
        pipelineDesc.depthStencil.depthTestEnable = false;
        pipelineDesc.depthStencil.depthWriteEnable = false;
        pipelineDesc.rasterizer.cullMode = CullMode::None;
        pipelineDesc.rasterizer.scissorEnable = true;
        pipelineDesc.compilationPolicy = PipelineCompilationPolicy::Immediate;
        pipelines[group] = device->createRenderPipeline(pipelineDesc);
        REQUIRE(pipelines[group]);
    }
    TextureDesc textureDesc;
    textureDesc.size = {8, 4, 1};
    textureDesc.format = Format::RGBA32Float;
    textureDesc.usage = TextureUsage::RenderTarget | TextureUsage::CopySource;
    auto texture = device->createTexture(textureDesc);
    REQUIRE(texture);
    auto view = device->createTextureView(texture, {});
    REQUIRE(view);
    auto queue = device->getQueue(QueueType::Graphics);
    auto encoder = queue->createCommandEncoder();
    RenderPassColorAttachment attachment;
    attachment.view = view;
    attachment.loadOp = LoadOp::Clear;
    RenderPassDesc passDesc;
    passDesc.colorAttachments = &attachment;
    passDesc.colorAttachmentCount = 1;
    auto pass = encoder->beginRenderPass(passDesc);
    for (uint32_t group = 0; group < 2; ++group)
    {
        auto root = pass->bindPipeline(pipelines[group]);
        float transform[16] = {1, 1};
        float color[4] = {group == 0 ? 1.f : 0.f, 0, 0, 1};
        float extra[4] = {0, 1, 0, 0};
        REQUIRE_CALL(root->getEntryPoint(0)->setData({}, transform, sizeof(transform)));
        REQUIRE_CALL(root->getEntryPoint(group + 1)->setData({}, color, sizeof(color)));
        if (group == 1)
        {
            ShaderOffset extraOffset;
            extraOffset.uniformOffset = 16;
            REQUIRE_CALL(root->getEntryPoint(2)->setData(extraOffset, extra, sizeof(extra)));
        }
        RenderState state;
        state.viewports[0] = Viewport::fromSize(8, 4);
        state.viewportCount = 1;
        state.scissorRects[0] = {group * 4, 0, (group + 1) * 4, 4};
        state.scissorRectCount = 1;
        pass->setRenderState(state);
        DrawArguments draw;
        draw.vertexCount = 3;
        pass->draw(draw);
    }
    pass->end();
    queue->submit(encoder->finish());
    queue->waitOnHost();
    ComPtr<ISlangBlob> pixels;
    SubresourceLayout pixelLayout;
    REQUIRE_CALL(device->readTexture(texture, 0, 0, pixels.writeRef(), &pixelLayout));
    for (uint32_t y = 0; y < 4; ++y)
    {
        auto row = reinterpret_cast<const float*>(
            static_cast<const uint8_t*>(pixels->getBufferPointer()) + y * pixelLayout.rowPitch
        );
        for (uint32_t x = 0; x < 8; ++x)
        {
            CHECK(row[x * 4] == (x < 4 ? 1.f : 0.f));
            CHECK(row[x * 4 + 1] == (x < 4 ? 0.f : 1.f));
            CHECK(row[x * 4 + 2] == 0.f);
            CHECK(row[x * 4 + 3] == 1.f);
        }
    }
}
