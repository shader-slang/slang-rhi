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
    vertexBufferDesc.defaultState = ResourceState::VertexBuffer;
    vertexBufferDesc.allowedStates = ResourceState::VertexBuffer;
    ComPtr<IBuffer> vertexBuffer = device->createBuffer(vertexBufferDesc, &kVertexData[0]);
    REQUIRE(vertexBuffer != nullptr);
    return vertexBuffer;
}

struct BaseResolveResourceTest
{
    IDevice* device;

    ComPtr<ITexture> msaaTexture;
    ComPtr<ITexture> dstTexture;

    ComPtr<ITransientResourceHeap> transientHeap;
    ComPtr<IPipeline> pipeline;
    ComPtr<IRenderPassLayout> renderPass;
    ComPtr<IFramebuffer> framebuffer;

    ComPtr<IBuffer> vertexBuffer;

    struct TextureInfo
    {
        Extents extent;
        int numMipLevels;
        int arraySize;
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
        msaaTexDesc.numMipLevels = dstTextureInfo.numMipLevels;
        msaaTexDesc.arraySize = dstTextureInfo.arraySize;
        msaaTexDesc.size = dstTextureInfo.extent;
        msaaTexDesc.defaultState = ResourceState::RenderTarget;
        msaaTexDesc.allowedStates = ResourceStateSet(ResourceState::RenderTarget, ResourceState::ResolveSource);
        msaaTexDesc.format = format;
        msaaTexDesc.sampleCount = 4;

        REQUIRE_CALL(device->createTexture(msaaTexDesc, msaaTextureInfo.initData, msaaTexture.writeRef()));

        TextureDesc dstTexDesc = {};
        dstTexDesc.type = TextureType::Texture2D;
        dstTexDesc.numMipLevels = dstTextureInfo.numMipLevels;
        dstTexDesc.arraySize = dstTextureInfo.arraySize;
        dstTexDesc.size = dstTextureInfo.extent;
        dstTexDesc.defaultState = ResourceState::ResolveDestination;
        dstTexDesc.allowedStates = ResourceStateSet(ResourceState::ResolveDestination, ResourceState::CopySource);
        dstTexDesc.format = format;

        REQUIRE_CALL(device->createTexture(dstTexDesc, dstTextureInfo.initData, dstTexture.writeRef()));

        IInputLayout::Desc inputLayoutDesc = {};
        inputLayoutDesc.inputElementCount = SLANG_COUNT_OF(inputElements);
        inputLayoutDesc.inputElements = inputElements;
        inputLayoutDesc.vertexStreamCount = SLANG_COUNT_OF(vertexStreams);
        inputLayoutDesc.vertexStreams = vertexStreams;
        auto inputLayout = device->createInputLayout(inputLayoutDesc);
        REQUIRE(inputLayout != nullptr);

        vertexBuffer = createVertexBuffer(device);

        ITransientResourceHeap::Desc transientHeapDesc = {};
        transientHeapDesc.constantBufferSize = 4096;
        REQUIRE_CALL(device->createTransientResourceHeap(transientHeapDesc, transientHeap.writeRef()));

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

        IFramebufferLayout::TargetLayout targetLayout;
        targetLayout.format = format;
        targetLayout.sampleCount = 4;

        IFramebufferLayout::Desc framebufferLayoutDesc;
        framebufferLayoutDesc.renderTargetCount = 1;
        framebufferLayoutDesc.renderTargets = &targetLayout;
        ComPtr<IFramebufferLayout> framebufferLayout = device->createFramebufferLayout(framebufferLayoutDesc);
        REQUIRE(framebufferLayout != nullptr);

        RenderPipelineDesc pipelineDesc = {};
        pipelineDesc.program = shaderProgram.get();
        pipelineDesc.inputLayout = inputLayout;
        pipelineDesc.framebufferLayout = framebufferLayout;
        pipelineDesc.depthStencil.depthTestEnable = false;
        pipelineDesc.depthStencil.depthWriteEnable = false;
        REQUIRE_CALL(device->createRenderPipeline(pipelineDesc, pipeline.writeRef()));

        IRenderPassLayout::Desc renderPassDesc = {};
        renderPassDesc.framebufferLayout = framebufferLayout;
        renderPassDesc.renderTargetCount = 1;
        IRenderPassLayout::TargetAccessDesc renderTargetAccess = {};
        renderTargetAccess.loadOp = IRenderPassLayout::TargetLoadOp::Clear;
        renderTargetAccess.storeOp = IRenderPassLayout::TargetStoreOp::Store;
        renderTargetAccess.initialState = ResourceState::RenderTarget;
        renderTargetAccess.finalState = ResourceState::ResolveSource;
        renderPassDesc.renderTargetAccess = &renderTargetAccess;
        REQUIRE_CALL(device->createRenderPassLayout(renderPassDesc, renderPass.writeRef()));

        IResourceView::Desc colorBufferViewDesc;
        memset(&colorBufferViewDesc, 0, sizeof(colorBufferViewDesc));
        colorBufferViewDesc.format = format;
        colorBufferViewDesc.renderTarget.shape = TextureType::Texture2D;
        colorBufferViewDesc.type = IResourceView::Type::RenderTarget;
        auto rtv = device->createTextureView(msaaTexture, colorBufferViewDesc);

        IFramebuffer::Desc framebufferDesc;
        framebufferDesc.renderTargetCount = 1;
        framebufferDesc.depthStencilView = nullptr;
        framebufferDesc.renderTargetViews = rtv.readRef();
        framebufferDesc.layout = framebufferLayout;
        REQUIRE_CALL(device->createFramebuffer(framebufferDesc, framebuffer.writeRef()));
    }

    void submitGPUWork(SubresourceRange msaaSubresource, SubresourceRange dstSubresource, Extents extent)
    {
        ComPtr<ITransientResourceHeap> transientHeap;
        ITransientResourceHeap::Desc transientHeapDesc = {};
        transientHeapDesc.constantBufferSize = 4096;
        REQUIRE_CALL(device->createTransientResourceHeap(transientHeapDesc, transientHeap.writeRef()));

        ICommandQueue::Desc queueDesc = {ICommandQueue::QueueType::Graphics};
        auto queue = device->createCommandQueue(queueDesc);

        auto commandBuffer = transientHeap->createCommandBuffer();
        auto renderEncoder = commandBuffer->encodeRenderCommands(renderPass, framebuffer);
        auto rootObject = renderEncoder->bindPipeline(pipeline);

        Viewport viewport = {};
        viewport.maxZ = 1.0f;
        viewport.extentX = kWidth;
        viewport.extentY = kHeight;
        renderEncoder->setViewportAndScissor(viewport);

        renderEncoder->setVertexBuffer(0, vertexBuffer);
        renderEncoder->setPrimitiveTopology(PrimitiveTopology::TriangleList);
        renderEncoder->draw(kVertexCount, 0);
        renderEncoder->endEncoding();

        auto resourceEncoder = commandBuffer->encodeResourceCommands();

        resourceEncoder->resolveResource(
            msaaTexture,
            ResourceState::ResolveSource,
            msaaSubresource,
            dstTexture,
            ResourceState::ResolveDestination,
            dstSubresource
        );
        resourceEncoder->textureSubresourceBarrier(
            dstTexture,
            dstSubresource,
            ResourceState::ResolveDestination,
            ResourceState::CopySource
        );
        resourceEncoder->endEncoding();
        commandBuffer->close();
        queue->executeCommandBuffer(commandBuffer);
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
        REQUIRE_CALL(
            device->readTexture(dstTexture, ResourceState::CopySource, resultBlob.writeRef(), &rowPitch, &pixelSize)
        );
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
        msaaSubresource.aspectMask = TextureAspect::Color;
        msaaSubresource.mipLevel = 0;
        msaaSubresource.mipLevelCount = 1;
        msaaSubresource.baseArrayLayer = 0;
        msaaSubresource.layerCount = 1;

        SubresourceRange dstSubresource = {};
        dstSubresource.aspectMask = TextureAspect::Color;
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
    runGpuTests(testResolveResource<ResolveResourceSimple>, {DeviceType::D3D12, DeviceType::Vulkan});
}
