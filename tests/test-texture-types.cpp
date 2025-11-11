#include "testing.h"

#include "texture-utils.h"

#include <string>

#if SLANG_WINDOWS_FAMILY
#include <d3d12.h>
#endif

using namespace rhi;
using namespace rhi::testing;

struct TextureTest
{
    IDevice* device;

    size_t alignedRowPitch;

    RefPtr<TextureInfo> textureInfo;
    RefPtr<ValidationTextureFormatBase> validationFormat;

    ComPtr<ITexture> texture;
    ComPtr<ITextureView> textureView;
    ComPtr<ISampler> sampler;
    ComPtr<IBuffer> resultsBuffer;

    const void* expectedTextureData;

    void init(IDevice* device_, Format format, RefPtr<ValidationTextureFormatBase> validationFormat_, TextureType type)
    {
        this->device = device_;
        this->validationFormat = validationFormat_;

        this->textureInfo = new TextureInfo();
        this->textureInfo->format = format;
        this->textureInfo->textureType = type;
    }
};

// used for shaderresource and unorderedaccess
struct TextureAccessTest : TextureTest
{
    bool readWrite;

    void init(
        IDevice* device_,
        Format format,
        RefPtr<ValidationTextureFormatBase> validationFormat_,
        TextureType type,
        bool readWrite_
    )
    {
        TextureTest::init(device_, format, validationFormat_, type);
        this->readWrite = readWrite_;
    }

    std::string getShaderEntryPoint()
    {
        std::string name = "test";

        name += readWrite ? "RWTexture" : "Texture";

        switch (textureInfo->textureType)
        {
        case TextureType::Texture1D:
            name += "1D";
            break;
        case TextureType::Texture2D:
            name += "2D";
            break;
        case TextureType::Texture3D:
            name += "3D";
            break;
        case TextureType::TextureCube:
            name += "Cube";
            break;
        default:
            FAIL("Unsupported texture type");
        }

        return name;
    }


    void createRequiredResources()
    {
        TextureDesc textureDesc = {};
        textureDesc.type = textureInfo->textureType;
        textureDesc.mipCount = textureInfo->mipCount;
        textureDesc.arrayLength = textureInfo->arrayLength;
        textureDesc.size = textureInfo->extent;
        textureDesc.usage = (readWrite ? TextureUsage::UnorderedAccess : TextureUsage::ShaderResource) |
                            TextureUsage::CopySource | TextureUsage::CopyDestination;
        textureDesc.defaultState = readWrite ? ResourceState::UnorderedAccess : ResourceState::ShaderResource;
        textureDesc.format = textureInfo->format;
        REQUIRE_CALL(device->createTexture(textureDesc, textureInfo->subresourceDatas.data(), texture.writeRef()));

        auto texelSize = getTexelSize(textureInfo->format);
        size_t alignment;
        device->getTextureRowAlignment(&alignment);
        alignedRowPitch = (textureInfo->extent.width * texelSize + alignment - 1) & ~(alignment - 1);
        BufferDesc bufferDesc = {};
        // All of the values read back from the shader will be uint32_t
        bufferDesc.size =
            textureDesc.size.width * textureDesc.size.height * textureDesc.size.depth * texelSize * sizeof(uint32_t);
        bufferDesc.format = Format::Undefined;
        bufferDesc.elementSize = sizeof(uint32_t);
        bufferDesc.usage = BufferUsage::UnorderedAccess | BufferUsage::CopyDestination | BufferUsage::CopySource;
        bufferDesc.defaultState = ResourceState::UnorderedAccess;
        bufferDesc.memoryType = MemoryType::DeviceLocal;

        REQUIRE_CALL(device->createBuffer(bufferDesc, nullptr, resultsBuffer.writeRef()));
    }

    void submitShaderWork(const char* entryPoint)
    {
        ComPtr<IShaderProgram> shaderProgram;
        REQUIRE_CALL(loadProgram(device, "test-texture-types", entryPoint, shaderProgram.writeRef()));

        ComputePipelineDesc pipelineDesc = {};
        pipelineDesc.program = shaderProgram.get();
        ComPtr<IComputePipeline> pipeline;
        REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

        // We have done all the set up work, now it is time to start recording a command buffer for
        // GPU execution.
        {
            auto queue = device->getQueue(QueueType::Graphics);
            auto commandEncoder = queue->createCommandEncoder();

            auto passEncoder = commandEncoder->beginComputePass();
            auto rootObject = passEncoder->bindPipeline(pipeline);
            ShaderCursor entryPointCursor(rootObject->getEntryPoint(0)); // get a cursor the the first entry-point.
            auto width = textureInfo->extent.width;
            auto height = textureInfo->extent.height;
            auto depth = textureInfo->extent.depth;
            entryPointCursor["width"].setData(width);
            entryPointCursor["height"].setData(height);
            entryPointCursor["depth"].setData(depth);
            entryPointCursor["texture"].setBinding(texture);
            entryPointCursor["results"].setBinding(resultsBuffer);
            if (sampler)
                entryPointCursor["sampler"].setBinding(sampler); // TODO: Bind nullptr and make sure it doesn't splut
            auto bufferElementCount = width * height * depth;
            passEncoder->dispatchCompute(bufferElementCount, 1, 1);
            passEncoder->end();

            queue->submit(commandEncoder->finish());
            queue->waitOnHost();
        }
    }

    void validateTextureValues(ValidationTextureData actual, ValidationTextureData original)
    {
        // TODO: needs to be extended to cover mip levels and array layers
        for (uint32_t x = 0; x < actual.extent.width; ++x)
        {
            for (uint32_t y = 0; y < actual.extent.height; ++y)
            {
                for (uint32_t z = 0; z < actual.extent.depth; ++z)
                {
                    auto actualBlock = (uint8_t*)actual.getBlockAt(x, y, z);
                    for (uint32_t i = 0; i < 4; ++i)
                    {
                        CHECK_EQ(actualBlock[i], 1);
                    }
                }
            }
        }
    }

    void checkTestResults()
    {
        // Only check writes if the texture can be written to
        if (readWrite)
        {
            ComPtr<ISlangBlob> textureBlob;
            SubresourceLayout layout;
            REQUIRE_CALL(device->readTexture(texture, 0, 0, textureBlob.writeRef(), &layout));
            auto textureValues = (uint8_t*)textureBlob->getBufferPointer();

            ValidationTextureData textureResults;
            textureResults.extent = textureInfo->extent;
            textureResults.textureData = textureValues;
            textureResults.pitches.x = (uint32_t)layout.colPitch;
            textureResults.pitches.y = (uint32_t)layout.rowPitch;
            textureResults.pitches.z = textureResults.extent.height * textureResults.pitches.y;

            ValidationTextureData originalData;
            originalData.extent = textureInfo->extent;
            originalData.textureData = textureInfo->subresourceDatas.data();
            originalData.pitches.x = (uint32_t)layout.colPitch;
            originalData.pitches.y = textureInfo->extent.width * originalData.pitches.x;
            originalData.pitches.z = textureInfo->extent.height * originalData.pitches.y;

            validateTextureValues(textureResults, originalData);
        }

        ComPtr<ISlangBlob> bufferBlob;
        REQUIRE_CALL(device->readBuffer(resultsBuffer, 0, resultsBuffer->getDesc().size, bufferBlob.writeRef()));
        auto results = (uint32_t*)bufferBlob->getBufferPointer();

        auto elementCount = textureInfo->extent.width * textureInfo->extent.height * textureInfo->extent.depth * 4;
        auto castedTextureData = (uint8_t*)expectedTextureData;
        for (uint32_t i = 0; i < elementCount; ++i)
        {
            CHECK_EQ(results[i], castedTextureData[i]);
        }
    }

    void run()
    {
        // TODO: Should test with samplers
        //             SamplerDesc samplerDesc;
        //             sampler = device->createSampler(samplerDesc);

        // TODO: Should test multiple mip levels and array layers
        textureInfo->extent.width = 4;
        textureInfo->extent.height = (textureInfo->textureType == TextureType::Texture1D) ? 1 : 4;
        textureInfo->extent.depth = (textureInfo->textureType != TextureType::Texture3D) ? 1 : 2;
        textureInfo->mipCount = 1;
        textureInfo->arrayLength = 1;
        generateTextureData(textureInfo, validationFormat);

        // We need to save the pointer to the original texture data for results checking because the texture will be
        // overwritten during testing (if the texture can be written to).
        expectedTextureData = textureInfo->subresourceDatas[0].data;

        createRequiredResources();
        auto entryPointName = getShaderEntryPoint();
        // printf("%s\n", entryPointName.getBuffer());
        submitShaderWork(entryPointName.c_str());

        checkTestResults();
    }
};

// used for rendertarget and depthstencil
struct RenderTargetTests : TextureTest
{
    struct Vertex
    {
        float position[3];
        float color[3];
    };

    const int kVertexCount = 12;
    const Vertex kVertexData[12] = {
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

    int sampleCount = 1;

    ComPtr<IRenderPipeline> pipeline;

    ComPtr<ITexture> renderTexture;
    ComPtr<ITextureView> renderTextureView;
    ComPtr<IBuffer> vertexBuffer;

    void createRequiredResources()
    {
        BufferDesc vertexBufferDesc;
        vertexBufferDesc.size = kVertexCount * sizeof(Vertex);
        vertexBufferDesc.usage = BufferUsage::VertexBuffer;
        vertexBufferDesc.defaultState = ResourceState::VertexBuffer;
        vertexBuffer = device->createBuffer(vertexBufferDesc, &kVertexData[0]);
        REQUIRE(vertexBuffer != nullptr);

        VertexStreamDesc vertexStreams[] = {
            {sizeof(Vertex), InputSlotClass::PerVertex, 0},
        };

        InputElementDesc inputElements[] = {
            // Vertex buffer data
            {"POSITION", 0, Format::RGB32Float, offsetof(Vertex, position), 0},
            {"COLOR", 0, Format::RGB32Float, offsetof(Vertex, color), 0},
        };

        TextureDesc renderTextureDesc = {};
        renderTextureDesc.type = textureInfo->textureType;
        renderTextureDesc.mipCount = textureInfo->mipCount;
        renderTextureDesc.arrayLength = textureInfo->arrayLength;
        renderTextureDesc.size = textureInfo->extent;
        renderTextureDesc.usage = TextureUsage::RenderTarget | TextureUsage::ResolveSource | TextureUsage::CopySource;
        renderTextureDesc.defaultState = ResourceState::RenderTarget;
        renderTextureDesc.format = textureInfo->format;
        renderTextureDesc.sampleCount = sampleCount;

        REQUIRE_CALL(
            device->createTexture(renderTextureDesc, textureInfo->subresourceDatas.data(), renderTexture.writeRef())
        );

        REQUIRE_CALL(device->createTextureView(renderTexture, {}, renderTextureView.writeRef()));

        TextureDesc textureDesc = {};
        textureDesc.type = textureInfo->textureType;
        textureDesc.mipCount = textureInfo->mipCount;
        textureDesc.arrayLength = textureInfo->arrayLength;
        textureDesc.size = textureInfo->extent;
        textureDesc.usage = TextureUsage::ResolveDestination | TextureUsage::CopySource;
        textureDesc.defaultState = ResourceState::ResolveDestination;
        textureDesc.format = textureInfo->format;

        REQUIRE_CALL(device->createTexture(textureDesc, textureInfo->subresourceDatas.data(), texture.writeRef()));

        REQUIRE_CALL(device->createTextureView(texture, {}, textureView.writeRef()));

        InputLayoutDesc inputLayoutDesc = {};
        inputLayoutDesc.inputElementCount = SLANG_COUNT_OF(inputElements);
        inputLayoutDesc.inputElements = inputElements;
        inputLayoutDesc.vertexStreamCount = SLANG_COUNT_OF(vertexStreams);
        inputLayoutDesc.vertexStreams = vertexStreams;
        auto inputLayout = device->createInputLayout(inputLayoutDesc);
        REQUIRE(inputLayout != nullptr);

        ComPtr<IShaderProgram> shaderProgram;
        REQUIRE_CALL(
            loadProgram(device, "test-texture-types", {"vertexMain", "fragmentMain"}, shaderProgram.writeRef())
        );

        ColorTargetDesc target;
        target.format = textureInfo->format;
        RenderPipelineDesc pipelineDesc = {};
        pipelineDesc.program = shaderProgram.get();
        pipelineDesc.inputLayout = inputLayout;
        pipelineDesc.targets = &target;
        pipelineDesc.targetCount = 1;
        pipelineDesc.depthStencil.depthTestEnable = false;
        pipelineDesc.depthStencil.depthWriteEnable = false;
        pipelineDesc.multisample.sampleCount = sampleCount;
        REQUIRE_CALL(device->createRenderPipeline(pipelineDesc, pipeline.writeRef()));

        auto texelSize = getTexelSize(textureInfo->format);
        size_t alignment;
        device->getTextureRowAlignment(&alignment);
        alignedRowPitch = (textureInfo->extent.width * texelSize + alignment - 1) & ~(alignment - 1);
    }

    void submitShaderWork()
    {
        auto queue = device->getQueue(QueueType::Graphics);
        auto commandEncoder = queue->createCommandEncoder();

        RenderPassColorAttachment colorAttachment;
        colorAttachment.view = renderTextureView;
        if (sampleCount > 1)
        {
            colorAttachment.resolveTarget = textureView;
        }
        colorAttachment.loadOp = LoadOp::Clear;
        colorAttachment.storeOp = StoreOp::Store;
        RenderPassDesc renderPass;
        renderPass.colorAttachments = &colorAttachment;
        renderPass.colorAttachmentCount = 1;
        auto passEncoder = commandEncoder->beginRenderPass(renderPass);

        passEncoder->bindPipeline(pipeline);

        RenderState state;
        state.viewports[0] = Viewport::fromSize(textureInfo->extent.width, textureInfo->extent.height);
        state.viewportCount = 1;
        state.scissorRects[0] = ScissorRect::fromSize(textureInfo->extent.width, textureInfo->extent.height);
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

    // TODO: Should take a value indicating the slice that was rendered into
    // TODO: Needs to handle either the correct slice or array layer (will not always check z)
    void validateTextureValues(ValidationTextureData actual)
    {
        for (uint32_t x = 0; x < actual.extent.width; ++x)
        {
            for (uint32_t y = 0; y < actual.extent.height; ++y)
            {
                for (uint32_t z = 0; z < actual.extent.depth; ++z)
                {
                    auto actualBlock = (float*)actual.getBlockAt(x, y, z);
                    for (uint32_t i = 0; i < 4; ++i)
                    {
                        if (z == 0)
                        {
                            // Slice being rendered into
                            CHECK_EQ(actualBlock[i], (float)i + 1);
                        }
                        else
                        {
                            CHECK_EQ(actualBlock[i], 0.0f);
                        }
                    }
                }
            }
        }
    }

    void checkTestResults()
    {
        ComPtr<ISlangBlob> textureBlob;
        SubresourceLayout layout;
        if (sampleCount > 1)
        {
            REQUIRE_CALL(device->readTexture(texture, 0, 0, textureBlob.writeRef(), &layout));
        }
        else
        {
            REQUIRE_CALL(device->readTexture(renderTexture, 0, 0, textureBlob.writeRef(), &layout));
        }
        auto textureValues = (float*)textureBlob->getBufferPointer();

        ValidationTextureData textureResults;
        textureResults.extent = textureInfo->extent;
        textureResults.textureData = textureValues;
        textureResults.pitches.x = (uint32_t)layout.colPitch;
        textureResults.pitches.y = (uint32_t)layout.rowPitch;
        textureResults.pitches.z = textureResults.extent.height * textureResults.pitches.y;

        validateTextureValues(textureResults);
    }

    void run()
    {
        // TODO: Sampler state and null state?
        //             SamplerDesc samplerDesc;
        //             sampler = device->createSampler(samplerDesc);

        textureInfo->extent.width = 4;
        textureInfo->extent.height = (textureInfo->textureType == TextureType::Texture1D) ? 1 : 4;
        textureInfo->extent.depth = (textureInfo->textureType != TextureType::Texture3D) ? 1 : 2;
        textureInfo->mipCount = 1;
        textureInfo->arrayLength = 1;
        generateTextureData(textureInfo, validationFormat);

        // We need to save the pointer to the original texture data for results checking because the texture will be
        // overwritten during testing (if the texture can be written to).
        expectedTextureData = textureInfo->subresourceDatas[0].data;

        createRequiredResources();
        submitShaderWork();

        checkTestResults();
    }
};

GPU_TEST_CASE("texture-types-shader", D3D12 | Vulkan | Metal)
{
    TextureType textureTypes[] = {
        TextureType::Texture1D,
        TextureType::Texture2D,
        TextureType::Texture3D,
        // TextureType::TextureCube,
    };

    for (TextureType textureType : textureTypes)
    {
        for (bool readWrite : {false, true})
        {
            auto format = Format::RGBA8Uint;
            auto validationFormat = getValidationTextureFormat(format);
            REQUIRE(validationFormat != nullptr);

            TextureAccessTest test;
            test.init(device, format, validationFormat, textureType, readWrite);
            test.run();
        }
    }
}

GPU_TEST_CASE("texture-types-render-target", D3D12 | Vulkan)
{
    TextureType textureTypes[] = {
        TextureType::Texture1D,
        TextureType::Texture2D,
        TextureType::Texture3D,
        // TextureType::TextureCube,
    };

    // TODO: Buffer and TextureCube
    for (TextureType textureType : textureTypes)
    {
        auto format = Format::RGBA32Float;
        auto validationFormat = getValidationTextureFormat(format);
        REQUIRE(validationFormat != nullptr);

        RenderTargetTests test;
        test.init(device, format, validationFormat, textureType);
        test.run();
    }
}

// 1D + array + multisample, ditto for 2D, ditto for 3D
// one test with something bound, one test with nothing bound, one test with subset of layers (set values in
// SubresourceRange and assign in desc)
