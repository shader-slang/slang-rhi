#include "testing.h"

#if SLANG_WINDOWS_FAMILY
#include <d3d12.h>
#endif

using namespace rhi;
using namespace rhi::testing;

struct Vertex
{
    float position[3];
    float color[3];
};

static const int kVertexCount = 12;
static const Vertex kVertexData[kVertexCount] = {
    // Triangle 1
    {{0, 0, 0.5}, {1, 0, 0}},
    {{1, 1, 0.5}, {1, 0, 0}},
    {{-1, 1, 0.5}, {1, 0, 0}},

    // Triangle 2
    {{-1, 1, 0.5}, {0, 1, 0}},
    {{0, 0, 0.5}, {0, 1, 0}},
    {{-1, -1, 0.5}, {0, 1, 0}},

    // Triangle 3
    {{-1, -1, 0.5}, {0, 0, 1}},
    {{0, 0, 0.5}, {0, 0, 1}},
    {{1, -1, 0.5}, {0, 0, 1}},

    // Triangle 4
    {{1, -1, 0.5}, {0, 0, 0}},
    {{0, 0, 0.5}, {0, 0, 0}},
    {{1, 1, 0.5}, {0, 0, 0}},
};

const int kWidth = 256;
const int kHeight = 256;
Format kFormat = Format::RGBA32Float;

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

struct BaseResolveResourceTest
{
    IDevice* device;

    ComPtr<ITexture> msaaTexture;
    ComPtr<ITextureView> msaaTextureView;
    ComPtr<ITexture> dstTexture;
    ComPtr<ITextureView> dstTextureView;

    ComPtr<IRenderPipeline> pipeline;

    ComPtr<IBuffer> vertexBuffer;

    struct TextureInfo
    {
        Extent3D extent;
        int mipCount;
        int arrayLength;
        const SubresourceData* initData;
    };

    void init(IDevice* device_) { this->device = device_; }

    void createRequiredResources(TextureInfo msaaTextureInfo, TextureInfo dstTextureInfo, Format format)
    {
        VertexStreamDesc vertexStreams[] = {
            {sizeof(Vertex), InputSlotClass::PerVertex, 0},
        };

        InputElementDesc inputElements[] = {
            // Vertex buffer data
            {"POSITION", 0, Format::RGB32Float, offsetof(Vertex, position), 0},
            {"COLOR", 0, Format::RGB32Float, offsetof(Vertex, color), 0},
        };

        TextureDesc msaaTexDesc = {};
        msaaTexDesc.type = TextureType::Texture2DMS;
        msaaTexDesc.mipCount = dstTextureInfo.mipCount;
        msaaTexDesc.arrayLength = dstTextureInfo.arrayLength;
        msaaTexDesc.size = dstTextureInfo.extent;
        msaaTexDesc.usage = TextureUsage::RenderTarget | TextureUsage::ResolveSource;
        msaaTexDesc.defaultState = ResourceState::RenderTarget;
        msaaTexDesc.format = format;
        msaaTexDesc.sampleCount = 4;

        REQUIRE_CALL(device->createTexture(msaaTexDesc, msaaTextureInfo.initData, msaaTexture.writeRef()));

        TextureDesc dstTexDesc = {};
        dstTexDesc.type = TextureType::Texture2D;
        dstTexDesc.mipCount = dstTextureInfo.mipCount;
        dstTexDesc.arrayLength = dstTextureInfo.arrayLength;
        dstTexDesc.size = dstTextureInfo.extent;
        dstTexDesc.usage = TextureUsage::ResolveDestination | TextureUsage::CopySource | TextureUsage::RenderTarget;
        dstTexDesc.defaultState = ResourceState::ResolveDestination;
        dstTexDesc.format = format;

        REQUIRE_CALL(device->createTexture(dstTexDesc, dstTextureInfo.initData, dstTexture.writeRef()));

        InputLayoutDesc inputLayoutDesc = {};
        inputLayoutDesc.inputElementCount = SLANG_COUNT_OF(inputElements);
        inputLayoutDesc.inputElements = inputElements;
        inputLayoutDesc.vertexStreamCount = SLANG_COUNT_OF(vertexStreams);
        inputLayoutDesc.vertexStreams = vertexStreams;
        auto inputLayout = device->createInputLayout(inputLayoutDesc);
        REQUIRE(inputLayout != nullptr);

        vertexBuffer = createVertexBuffer(device);

        ComPtr<IShaderProgram> shaderProgram;
        REQUIRE_CALL(loadProgram(
            device,
            nullptr,
            "test-resolve-resource-shader",
            {"vertexMain", "fragmentMain"},
            shaderProgram.writeRef()
        ));


        ColorTargetDesc target;
        target.format = format;
        RenderPipelineDesc pipelineDesc = {};
        pipelineDesc.program = shaderProgram.get();
        pipelineDesc.inputLayout = inputLayout;
        pipelineDesc.targets = &target;
        pipelineDesc.targetCount = 1;
        pipelineDesc.depthStencil.depthTestEnable = false;
        pipelineDesc.depthStencil.depthWriteEnable = false;
        pipelineDesc.multisample.sampleCount = 4;
        REQUIRE_CALL(device->createRenderPipeline(pipelineDesc, pipeline.writeRef()));

        TextureViewDesc textureViewDesc = {};
        textureViewDesc.format = format;
        REQUIRE_CALL(device->createTextureView(msaaTexture, textureViewDesc, msaaTextureView.writeRef()));
        REQUIRE_CALL(device->createTextureView(dstTexture, textureViewDesc, dstTextureView.writeRef()));
    }

    void submitGPUWork(SubresourceRange msaaSubresource, SubresourceRange dstSubresource, Extent3D extent)
    {
        auto queue = device->getQueue(QueueType::Graphics);
        auto commandEncoder = queue->createCommandEncoder();

        RenderPassColorAttachment colorAttachment;
        colorAttachment.view = msaaTextureView;
        colorAttachment.resolveTarget = dstTextureView;
        colorAttachment.loadOp = LoadOp::Clear;
        colorAttachment.storeOp = StoreOp::Store;
        RenderPassDesc renderPass;
        renderPass.colorAttachments = &colorAttachment;
        renderPass.colorAttachmentCount = 1;
        auto passEncoder = commandEncoder->beginRenderPass(renderPass);

        passEncoder->bindPipeline(pipeline);

        RenderState state;
        state.viewports[0] = Viewport::fromSize(extent.width, extent.height);
        state.viewportCount = 1;
        state.scissorRects[0] = ScissorRect::fromSize(extent.width, extent.height);
        state.scissorRectCount = 1;
        state.vertexBuffers[0] = vertexBuffer;
        state.vertexBufferCount = 1;
        passEncoder->setRenderState(state);

        DrawArguments args;
        args.vertexCount = kVertexCount;
        passEncoder->draw(args);
        passEncoder->end();

        queue->submit(commandEncoder->finish());
        queue->waitOnHost();
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
        REQUIRE_CALL(device->readTexture(dstTexture, 0, 0, resultBlob.writeRef(), &layout));
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

        float expectedResult[] = {0.5f, 0.5f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.5f, 0.0f, 0.0f,
                                  1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.5f,
                                  0.5f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.5f, 1.0f};
        CHECK(memcmp(testResults, expectedResult, 128) == 0);
    }
};

// TODO: Add more tests?

struct ResolveResourceSimple : BaseResolveResourceTest
{
    void run()
    {
        Extent3D extent = {};
        extent.width = kWidth;
        extent.height = kHeight;
        extent.depth = 1;

        TextureInfo msaaTextureInfo = {extent, 1, 1, nullptr};
        TextureInfo dstTextureInfo = {extent, 1, 1, nullptr};

        createRequiredResources(msaaTextureInfo, dstTextureInfo, kFormat);

        SubresourceRange msaaSubresource = {};
        msaaSubresource.layer = 0;
        msaaSubresource.layerCount = 1;
        msaaSubresource.mip = 0;
        msaaSubresource.mipCount = 1;

        SubresourceRange dstSubresource = {};
        dstSubresource.layer = 0;
        dstSubresource.layerCount = 1;
        dstSubresource.mip = 0;
        dstSubresource.mipCount = 1;

        submitGPUWork(msaaSubresource, dstSubresource, extent);

        const int kPixelCount = 8;
        const int kChannelCount = 4;
        int testXCoords[kPixelCount] = {64, 127, 191, 64, 191, 64, 127, 191};
        int testYCoords[kPixelCount] = {64, 64, 64, 127, 127, 191, 191, 191};
        float testResults[kPixelCount * kChannelCount];

        checkTestResults(kPixelCount, kChannelCount, testXCoords, testYCoords, testResults);
    }
};

// skip WGPU: rgba32float is not supported for multisampling
GPU_TEST_CASE("resolve-resource-simple", D3D11 | D3D12 | Vulkan | Metal)
{
    ResolveResourceSimple test;
    test.init(device);
    test.run();
}
