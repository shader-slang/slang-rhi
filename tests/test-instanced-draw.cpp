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
const Format format = Format::R32G32B32A32_FLOAT;

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
    colorBufferDesc.mipLevelCount = 1;
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

    ComPtr<ITransientResourceHeap> transientHeap;
    ComPtr<IPipeline> pipeline;

    ComPtr<IBuffer> vertexBuffer;
    ComPtr<IBuffer> instanceBuffer;
    ComPtr<ITexture> colorBuffer;
    ComPtr<ITextureView> colorBufferView;

    void init(IDevice* device) { this->device = device; }

    void createRequiredResources()
    {
        VertexStreamDesc vertexStreams[] = {
            {sizeof(Vertex), InputSlotClass::PerVertex, 0},
            {sizeof(Instance), InputSlotClass::PerInstance, 1},
        };

        InputElementDesc inputElements[] = {
            // Vertex buffer data
            {"POSITIONA", 0, Format::R32G32B32_FLOAT, offsetof(Vertex, position), 0},

            // Instance buffer data
            {"POSITIONB", 0, Format::R32G32B32_FLOAT, offsetof(Instance, position), 1},
            {"COLOR", 0, Format::R32G32B32_FLOAT, offsetof(Instance, color), 1},
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

        ITransientResourceHeap::Desc transientHeapDesc = {};
        transientHeapDesc.constantBufferSize = 4096;
        REQUIRE_CALL(device->createTransientResourceHeap(transientHeapDesc, transientHeap.writeRef()));

        ComPtr<IShaderProgram> shaderProgram;
        slang::ProgramLayout* slangReflection;
        REQUIRE_CALL(loadGraphicsProgram(
            device,
            shaderProgram,
            "test-instanced-draw",
            "vertexMain",
            "fragmentMain",
            slangReflection
        ));

        ColorTargetState colorTarget;
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
        size_t rowPitch = 0;
        size_t pixelSize = 0;
        REQUIRE_CALL(device->readTexture(colorBuffer, resultBlob.writeRef(), &rowPitch, &pixelSize));
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

        float expectedResult[] =
            {1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f};
        compareComputeResultFuzzy(testResults, expectedResult, sizeof(expectedResult));
    }
};

struct DrawInstancedTest : BaseDrawTest
{
    void setUpAndDraw()
    {
        createRequiredResources();

        auto queue = device->getQueue(QueueType::Graphics);
        auto commandBuffer = transientHeap->createCommandBuffer();

        RenderPassColorAttachment colorAttachment;
        colorAttachment.view = colorBufferView;
        colorAttachment.loadOp = LoadOp::Clear;
        colorAttachment.storeOp = StoreOp::Store;
        RenderPassDesc renderPass;
        renderPass.colorAttachments = &colorAttachment;
        renderPass.colorAttachmentCount = 1;

        auto passEncoder = commandBuffer->beginRenderPass(renderPass);
        auto rootObject = passEncoder->bindPipeline(pipeline);

        Viewport viewport = {};
        viewport.maxZ = 1.0f;
        viewport.extentX = kWidth;
        viewport.extentY = kHeight;
        passEncoder->setViewportAndScissor(viewport);

        uint32_t startVertex = 0;
        uint32_t startInstanceLocation = 0;

        passEncoder->setVertexBuffer(0, vertexBuffer);
        passEncoder->setVertexBuffer(1, instanceBuffer);

        passEncoder->drawInstanced(kVertexCount, kInstanceCount, startVertex, startInstanceLocation);
        passEncoder->end();
        commandBuffer->close();
        queue->executeCommandBuffer(commandBuffer);
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
        auto commandBuffer = transientHeap->createCommandBuffer();

        RenderPassColorAttachment colorAttachment;
        colorAttachment.view = colorBufferView;
        colorAttachment.loadOp = LoadOp::Clear;
        colorAttachment.storeOp = StoreOp::Store;
        RenderPassDesc renderPass;
        renderPass.colorAttachments = &colorAttachment;
        renderPass.colorAttachmentCount = 1;

        auto passEncoder = commandBuffer->beginRenderPass(renderPass);
        auto rootObject = passEncoder->bindPipeline(pipeline);

        Viewport viewport = {};
        viewport.maxZ = 1.0f;
        viewport.extentX = kWidth;
        viewport.extentY = kHeight;
        passEncoder->setViewportAndScissor(viewport);

        uint32_t startIndex = 0;
        int32_t startVertex = 0;
        uint32_t startInstanceLocation = 0;

        passEncoder->setVertexBuffer(0, vertexBuffer);
        passEncoder->setVertexBuffer(1, instanceBuffer);
        passEncoder->setIndexBuffer(indexBuffer, IndexFormat::UInt32);

        passEncoder->drawIndexedInstanced(kIndexCount, kInstanceCount, startIndex, startVertex, startInstanceLocation);
        passEncoder->end();
        commandBuffer->close();
        queue->executeCommandBuffer(commandBuffer);
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

    ComPtr<IBuffer> createIndirectBuffer(IDevice* device)
    {
        static const IndirectArgData kIndirectData = {
            42.0f,        // padding
            {6, 2, 0, 0}, // args
        };

        BufferDesc indirectBufferDesc;
        indirectBufferDesc.size = sizeof(IndirectArgData);
        indirectBufferDesc.usage = BufferUsage::IndirectArgument;
        indirectBufferDesc.defaultState = ResourceState::IndirectArgument;
        ComPtr<IBuffer> indirectBuffer = device->createBuffer(indirectBufferDesc, &kIndirectData);
        REQUIRE(indirectBuffer != nullptr);
        return indirectBuffer;
    }

    void setUpAndDraw()
    {
        createRequiredResources();

        auto queue = device->getQueue(QueueType::Graphics);
        auto commandBuffer = transientHeap->createCommandBuffer();

        RenderPassColorAttachment colorAttachment;
        colorAttachment.view = colorBufferView;
        colorAttachment.loadOp = LoadOp::Clear;
        colorAttachment.storeOp = StoreOp::Store;
        RenderPassDesc renderPass;
        renderPass.colorAttachments = &colorAttachment;
        renderPass.colorAttachmentCount = 1;

        auto passEncoder = commandBuffer->beginRenderPass(renderPass);
        auto rootObject = passEncoder->bindPipeline(pipeline);

        Viewport viewport = {};
        viewport.maxZ = 1.0f;
        viewport.extentX = kWidth;
        viewport.extentY = kHeight;
        passEncoder->setViewportAndScissor(viewport);

        passEncoder->setVertexBuffer(0, vertexBuffer);
        passEncoder->setVertexBuffer(1, instanceBuffer);

        uint32_t maxDrawCount = 1;
        Offset argOffset = offsetof(IndirectArgData, args);

        passEncoder->drawIndirect(maxDrawCount, indirectBuffer, argOffset);
        passEncoder->end();
        commandBuffer->close();
        queue->executeCommandBuffer(commandBuffer);
        queue->waitOnHost();
    }

    void run()
    {
        indirectBuffer = createIndirectBuffer(device);

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

    ComPtr<IBuffer> createIndirectBuffer(IDevice* device)
    {
        static const IndexedIndirectArgData kIndexedIndirectData = {
            42.0f,           // padding
            {6, 2, 0, 0, 0}, // args
        };

        BufferDesc indirectBufferDesc;
        indirectBufferDesc.size = sizeof(IndexedIndirectArgData);
        indirectBufferDesc.usage = BufferUsage::IndirectArgument;
        indirectBufferDesc.defaultState = ResourceState::IndirectArgument;
        ComPtr<IBuffer> indexBuffer = device->createBuffer(indirectBufferDesc, &kIndexedIndirectData);
        REQUIRE(indexBuffer != nullptr);
        return indexBuffer;
    }

    void setUpAndDraw()
    {
        createRequiredResources();

        auto queue = device->getQueue(QueueType::Graphics);
        auto commandBuffer = transientHeap->createCommandBuffer();

        RenderPassColorAttachment colorAttachment;
        colorAttachment.view = colorBufferView;
        colorAttachment.loadOp = LoadOp::Clear;
        colorAttachment.storeOp = StoreOp::Store;
        RenderPassDesc renderPass;
        renderPass.colorAttachments = &colorAttachment;
        renderPass.colorAttachmentCount = 1;

        auto passEncoder = commandBuffer->beginRenderPass(renderPass);
        auto rootObject = passEncoder->bindPipeline(pipeline);

        Viewport viewport = {};
        viewport.maxZ = 1.0f;
        viewport.extentX = kWidth;
        viewport.extentY = kHeight;
        passEncoder->setViewportAndScissor(viewport);

        passEncoder->setVertexBuffer(0, vertexBuffer);
        passEncoder->setVertexBuffer(1, instanceBuffer);
        passEncoder->setIndexBuffer(indexBuffer, IndexFormat::UInt32);

        uint32_t maxDrawCount = 1;
        Offset argOffset = offsetof(IndexedIndirectArgData, args);

        passEncoder->drawIndexedIndirect(maxDrawCount, indirectBuffer, argOffset);
        passEncoder->end();
        commandBuffer->close();
        queue->executeCommandBuffer(commandBuffer);
        queue->waitOnHost();
    }

    void run()
    {
        indexBuffer = createIndexBuffer(device);
        indirectBuffer = createIndirectBuffer(device);

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
void testDraw(GpuTestContext* ctx, DeviceType deviceType)
{
    ComPtr<IDevice> device = createTestingDevice(ctx, deviceType);
    T test;
    test.init(device);
    test.run();
}

TEST_CASE("draw-instanced")
{
    runGpuTests(
        testDraw<DrawInstancedTest>,
        {
            DeviceType::D3D11,
            DeviceType::D3D12,
            DeviceType::Vulkan,
            DeviceType::Metal,
        }
    );
}

TEST_CASE("draw-indexed-instanced")
{
    runGpuTests(
        testDraw<DrawIndexedInstancedTest>,
        {
            DeviceType::D3D11,
            DeviceType::D3D12,
            DeviceType::Vulkan,
            DeviceType::Metal,
        }
    );
}

TEST_CASE("draw-indirect")
{
    runGpuTests(
        testDraw<DrawIndirectTest>,
        {
            DeviceType::D3D12,
            DeviceType::Vulkan,
        }
    );
}

TEST_CASE("draw-indexed-indirect")
{
    runGpuTests(
        testDraw<DrawIndexedIndirectTest>,
        {
            DeviceType::D3D12,
            DeviceType::Vulkan,
        }
    );
}
