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
Format format = Format::R32G32B32A32_FLOAT;

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
        Extents extent;
        int mipLevelCount;
        int arrayLength;
        SubresourceData const* initData;
    };

    void init(IDevice* device) { this->device = device; }

    void createRequiredResources(TextureInfo msaaTextureInfo, TextureInfo dstTextureInfo, Format format)
    {
        VertexStreamDesc vertexStreams[] = {
            {sizeof(Vertex), InputSlotClass::PerVertex, 0},
        };

        InputElementDesc inputElements[] = {
            // Vertex buffer data
            {"POSITION", 0, Format::R32G32B32_FLOAT, offsetof(Vertex, position), 0},
            {"COLOR", 0, Format::R32G32B32_FLOAT, offsetof(Vertex, color), 0},
        };

        TextureDesc msaaTexDesc = {};
        msaaTexDesc.type = TextureType::Texture2D;
        msaaTexDesc.mipLevelCount = dstTextureInfo.mipLevelCount;
        msaaTexDesc.arrayLength = dstTextureInfo.arrayLength;
        msaaTexDesc.size = dstTextureInfo.extent;
        msaaTexDesc.usage = TextureUsage::RenderTarget | TextureUsage::ResolveSource;
        msaaTexDesc.defaultState = ResourceState::RenderTarget;
        msaaTexDesc.format = format;
        msaaTexDesc.sampleCount = 4;

        REQUIRE_CALL(device->createTexture(msaaTexDesc, msaaTextureInfo.initData, msaaTexture.writeRef()));

        TextureDesc dstTexDesc = {};
        dstTexDesc.type = TextureType::Texture2D;
        dstTexDesc.mipLevelCount = dstTextureInfo.mipLevelCount;
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
        slang::ProgramLayout* slangReflection;
        REQUIRE_CALL(loadGraphicsProgram(
            device,
            shaderProgram,
            "test-resolve-resource-shader",
            "vertexMain",
            "fragmentMain",
            slangReflection
        ));


        ColorTargetState target;
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

    void submitGPUWork(SubresourceRange msaaSubresource, SubresourceRange dstSubresource, Extents extent)
    {
        auto queue = device->getQueue(QueueType::Graphics);
        auto encoder = queue->createCommandEncoder();

        auto rootObject = device->createRootShaderObject(pipeline);
        rootObject->finalize();

        RenderPassColorAttachment colorAttachment;
        colorAttachment.view = msaaTextureView;
        colorAttachment.resolveTarget = dstTextureView;
        colorAttachment.loadOp = LoadOp::Clear;
        colorAttachment.storeOp = StoreOp::Store;
        RenderPassDesc renderPass;
        renderPass.colorAttachments = &colorAttachment;
        renderPass.colorAttachmentCount = 1;
        auto passEncoder = encoder->beginRenderPass(renderPass);

        RenderState state;
        state.pipeline = pipeline;
        state.rootObject = rootObject;
        state.viewports[0] = Viewport(extent.width, extent.height);
        state.viewportCount = 1;
        state.scissorRects[0] = ScissorRect(extent.width, extent.height);
        state.scissorRectCount = 1;
        state.vertexBuffers[0] = vertexBuffer;
        state.vertexBufferCount = 1;
        passEncoder->setRenderState(state);

        DrawArguments args;
        args.vertexCount = kVertexCount;
        passEncoder->draw(args);
        passEncoder->end();

        queue->submit(encoder->finish());
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
        size_t rowPitch = 0;
        size_t pixelSize = 0;
        REQUIRE_CALL(device->readTexture(dstTexture, resultBlob.writeRef(), &rowPitch, &pixelSize));
        auto result = (float*)resultBlob->getBufferPointer();

        int cursor = 0;
        for (int i = 0; i < pixelCount; ++i)
        {
            auto x = testXCoords[i];
            auto y = testYCoords[i];
            auto pixelPtr = result + x * channelCount + y * rowPitch / sizeof(float);
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
        Extents extent = {};
        extent.width = kWidth;
        extent.height = kHeight;
        extent.depth = 1;

        TextureInfo msaaTextureInfo = {extent, 1, 1, nullptr};
        TextureInfo dstTextureInfo = {extent, 1, 1, nullptr};

        createRequiredResources(msaaTextureInfo, dstTextureInfo, format);

        SubresourceRange msaaSubresource = {};
        msaaSubresource.mipLevel = 0;
        msaaSubresource.mipLevelCount = 1;
        msaaSubresource.baseArrayLayer = 0;
        msaaSubresource.layerCount = 1;

        SubresourceRange dstSubresource = {};
        dstSubresource.mipLevel = 0;
        dstSubresource.mipLevelCount = 1;
        dstSubresource.baseArrayLayer = 0;
        dstSubresource.layerCount = 1;

        submitGPUWork(msaaSubresource, dstSubresource, extent);

        const int kPixelCount = 8;
        const int kChannelCount = 4;
        int testXCoords[kPixelCount] = {64, 127, 191, 64, 191, 64, 127, 191};
        int testYCoords[kPixelCount] = {64, 64, 64, 127, 127, 191, 191, 191};
        float testResults[kPixelCount * kChannelCount];

        checkTestResults(kPixelCount, kChannelCount, testXCoords, testYCoords, testResults);
    }
};

template<typename T>
void testResolveResource(GpuTestContext* ctx, DeviceType deviceType)
{
    ComPtr<IDevice> device = createTestingDevice(ctx, deviceType);
    T test;
    test.init(device);
    test.run();
}

TEST_CASE("resolve-resource-simple")
{
    // Only supported on D3D12 and Vulkan.
    runGpuTests(
        testResolveResource<ResolveResourceSimple>,
        {
            DeviceType::D3D11,
            DeviceType::D3D12,
            DeviceType::Vulkan,
            DeviceType::Metal,
        }
    );
}
