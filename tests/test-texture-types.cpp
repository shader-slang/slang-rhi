#include "testing.h"

#include "texture-utils.h"

#include <string>

#if SLANG_WINDOWS_FAMILY
#include <d3d12.h>
#endif

using namespace rhi;
using namespace rhi::testing;

struct BaseTextureViewTest
{
    IDevice* device;

    IResourceView::Type viewType;
    size_t alignedRowStride;

    RefPtr<TextureInfo> textureInfo;
    RefPtr<ValidationTextureFormatBase> validationFormat;

    ComPtr<ITexture> texture;
    ComPtr<IResourceView> textureView;
    ComPtr<IBuffer> resultsBuffer;
    ComPtr<IResourceView> bufferView;

    ComPtr<ISampler> sampler;

    const void* expectedTextureData;

    void init(
        IDevice* device,
        Format format,
        RefPtr<ValidationTextureFormatBase> validationFormat,
        IResourceView::Type viewType,
        TextureType type
    )
    {
        this->device = device;
        this->validationFormat = validationFormat;
        this->viewType = viewType;

        this->textureInfo = new TextureInfo();
        this->textureInfo->format = format;
        this->textureInfo->textureType = type;
    }

    ResourceState getDefaultResourceStateForViewType(IResourceView::Type type)
    {
        switch (type)
        {
        case IResourceView::Type::RenderTarget:
            return ResourceState::RenderTarget;
        case IResourceView::Type::DepthStencil:
            return ResourceState::DepthWrite;
        case IResourceView::Type::ShaderResource:
            return ResourceState::ShaderResource;
        case IResourceView::Type::UnorderedAccess:
            return ResourceState::UnorderedAccess;
        case IResourceView::Type::AccelerationStructure:
            return ResourceState::AccelerationStructure;
        default:
            return ResourceState::Undefined;
        }
    }

    std::string getShaderEntryPoint()
    {
        std::string base = "resourceViewTest";
        std::string shape;
        std::string view;

        switch (textureInfo->textureType)
        {
        case TextureType::Texture1D:
            shape = "1D";
            break;
        case TextureType::Texture2D:
            shape = "2D";
            break;
        case TextureType::Texture3D:
            shape = "3D";
            break;
        case TextureType::TextureCube:
            shape = "Cube";
            break;
        default:
            MESSAGE("Invalid texture shape");
            REQUIRE(false);
        }

        switch (viewType)
        {
        case IResourceView::Type::RenderTarget:
            view = "Render";
            break;
        case IResourceView::Type::DepthStencil:
            view = "Depth";
            break;
        case IResourceView::Type::ShaderResource:
            view = "Shader";
            break;
        case IResourceView::Type::UnorderedAccess:
            view = "Unordered";
            break;
        case IResourceView::Type::AccelerationStructure:
            view = "Accel";
            break;
        default:
            MESSAGE("Invalid resource view");
            REQUIRE(false);
        }

        return base + shape + view;
    }
};

// used for shaderresource and unorderedaccess
struct ShaderAndUnorderedTests : BaseTextureViewTest
{
    void createRequiredResources()
    {
        TextureDesc textureDesc = {};
        textureDesc.type = textureInfo->textureType;
        textureDesc.numMipLevels = textureInfo->mipLevelCount;
        textureDesc.arraySize = textureInfo->arrayLayerCount;
        textureDesc.size = textureInfo->extents;
        textureDesc.defaultState = getDefaultResourceStateForViewType(viewType);
        textureDesc.allowedStates =
            ResourceStateSet(textureDesc.defaultState, ResourceState::CopySource, ResourceState::CopyDestination);
        textureDesc.format = textureInfo->format;

        REQUIRE_CALL(device->createTexture(textureDesc, textureInfo->subresourceDatas.data(), texture.writeRef()));

        IResourceView::Desc textureViewDesc = {};
        textureViewDesc.type = viewType;
        textureViewDesc.format = textureDesc.format; // TODO: Handle typeless formats - rhiIsTypelessFormat(format) ?
                                                     // convertTypelessFormat(format) : format;
        REQUIRE_CALL(device->createTextureView(texture, textureViewDesc, textureView.writeRef()));

        auto texelSize = getTexelSize(textureInfo->format);
        size_t alignment;
        device->getTextureRowAlignment(&alignment);
        alignedRowStride = (textureInfo->extents.width * texelSize + alignment - 1) & ~(alignment - 1);
        BufferDesc bufferDesc = {};
        // All of the values read back from the shader will be uint32_t
        bufferDesc.size =
            textureDesc.size.width * textureDesc.size.height * textureDesc.size.depth * texelSize * sizeof(uint32_t);
        bufferDesc.format = Format::Unknown;
        bufferDesc.elementSize = sizeof(uint32_t);
        bufferDesc.usage = BufferUsage::UnorderedAccess | BufferUsage::CopyDestination | BufferUsage::CopySource;
        bufferDesc.defaultState = ResourceState::UnorderedAccess;
        bufferDesc.memoryType = MemoryType::DeviceLocal;

        REQUIRE_CALL(device->createBuffer(bufferDesc, nullptr, resultsBuffer.writeRef()));

        IResourceView::Desc bufferViewDesc = {};
        bufferViewDesc.type = IResourceView::Type::UnorderedAccess;
        bufferViewDesc.format = Format::Unknown;
        REQUIRE_CALL(device->createBufferView(resultsBuffer, nullptr, bufferViewDesc, bufferView.writeRef()));
    }

    void submitShaderWork(const char* entryPoint)
    {
        ComPtr<ITransientResourceHeap> transientHeap;
        ITransientResourceHeap::Desc transientHeapDesc = {};
        transientHeapDesc.constantBufferSize = 4096;
        REQUIRE_CALL(device->createTransientResourceHeap(transientHeapDesc, transientHeap.writeRef()));

        ComPtr<IShaderProgram> shaderProgram;
        slang::ProgramLayout* slangReflection;
        REQUIRE_CALL(loadComputeProgram(device, shaderProgram, "test-texture-types", entryPoint, slangReflection));

        ComputePipelineDesc pipelineDesc = {};
        pipelineDesc.program = shaderProgram.get();
        ComPtr<IPipeline> pipeline;
        REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

        // We have done all the set up work, now it is time to start recording a command buffer for
        // GPU execution.
        {
            ICommandQueue::Desc queueDesc = {ICommandQueue::QueueType::Graphics};
            auto queue = device->createCommandQueue(queueDesc);

            auto commandBuffer = transientHeap->createCommandBuffer();
            auto encoder = commandBuffer->encodeComputeCommands();

            auto rootObject = encoder->bindPipeline(pipeline);

            ShaderCursor entryPointCursor(rootObject->getEntryPoint(0)); // get a cursor the the first entry-point.

            auto width = textureInfo->extents.width;
            auto height = textureInfo->extents.height;
            auto depth = textureInfo->extents.depth;

            entryPointCursor["width"].setData(width);
            entryPointCursor["height"].setData(height);
            entryPointCursor["depth"].setData(depth);

            // Bind texture view to the entry point
            entryPointCursor["resourceView"].setResource(textureView); // TODO: Bind nullptr and make sure it doesn't
                                                                       // splut - should be 0 everywhere
            entryPointCursor["testResults"].setResource(bufferView);

            if (sampler)
                entryPointCursor["sampler"].setSampler(sampler); // TODO: Bind nullptr and make sure it doesn't splut

            auto bufferElementCount = width * height * depth;
            encoder->dispatchCompute(bufferElementCount, 1, 1);

            encoder->textureBarrier(texture, texture->getDesc()->defaultState, ResourceState::CopySource);

            encoder->endEncoding();
            commandBuffer->close();
            queue->executeCommandBuffer(commandBuffer);
            queue->waitOnHost();
        }
    }

    void validateTextureValues(ValidationTextureData actual, ValidationTextureData original)
    {
        // TODO: needs to be extended to cover mip levels and array layers
        for (GfxIndex x = 0; x < actual.extents.width; ++x)
        {
            for (GfxIndex y = 0; y < actual.extents.height; ++y)
            {
                for (GfxIndex z = 0; z < actual.extents.depth; ++z)
                {
                    auto actualBlock = (uint8_t*)actual.getBlockAt(x, y, z);
                    for (Int i = 0; i < 4; ++i)
                    {
                        CHECK_EQ(actualBlock[i], 1);
                    }
                }
            }
        }
    }

    void checkTestResults()
    {
        // Shader resources are read-only, so we don't need to check that writes to the resource were correct.
        if (viewType != IResourceView::Type::ShaderResource)
        {
            ComPtr<ISlangBlob> textureBlob;
            size_t rowPitch;
            size_t pixelSize;
            REQUIRE_CALL(
                device->readTexture(texture, ResourceState::CopySource, textureBlob.writeRef(), &rowPitch, &pixelSize)
            );
            auto textureValues = (uint8_t*)textureBlob->getBufferPointer();

            ValidationTextureData textureResults;
            textureResults.extents = textureInfo->extents;
            textureResults.textureData = textureValues;
            textureResults.strides.x = (uint32_t)pixelSize;
            textureResults.strides.y = (uint32_t)rowPitch;
            textureResults.strides.z = textureResults.extents.height * textureResults.strides.y;

            ValidationTextureData originalData;
            originalData.extents = textureInfo->extents;
            originalData.textureData = textureInfo->subresourceDatas.data();
            originalData.strides.x = (uint32_t)pixelSize;
            originalData.strides.y = textureInfo->extents.width * originalData.strides.x;
            originalData.strides.z = textureInfo->extents.height * originalData.strides.y;

            validateTextureValues(textureResults, originalData);
        }

        ComPtr<ISlangBlob> bufferBlob;
        REQUIRE_CALL(device->readBuffer(resultsBuffer, 0, resultsBuffer->getDesc()->size, bufferBlob.writeRef()));
        auto results = (uint32_t*)bufferBlob->getBufferPointer();

        auto elementCount = textureInfo->extents.width * textureInfo->extents.height * textureInfo->extents.depth * 4;
        auto castedTextureData = (uint8_t*)expectedTextureData;
        for (Int i = 0; i < elementCount; ++i)
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
        textureInfo->extents.width = 4;
        textureInfo->extents.height = (textureInfo->textureType == TextureType::Texture1D) ? 1 : 4;
        textureInfo->extents.depth = (textureInfo->textureType != TextureType::Texture3D) ? 1 : 2;
        textureInfo->mipLevelCount = 1;
        textureInfo->arrayLayerCount = 1;
        generateTextureData(textureInfo, validationFormat);

        // We need to save the pointer to the original texture data for results checking because the texture will be
        // overwritten during testing (if the texture can be written to).
        expectedTextureData = textureInfo->subresourceDatas[getSubresourceIndex(0, 1, 0)].data;

        createRequiredResources();
        auto entryPointName = getShaderEntryPoint();
        // printf("%s\n", entryPointName.getBuffer());
        submitShaderWork(entryPointName.c_str());

        checkTestResults();
    }
};

// used for rendertarget and depthstencil
struct RenderTargetTests : BaseTextureViewTest
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

    ComPtr<ITransientResourceHeap> transientHeap;
    ComPtr<IPipeline> pipeline;

    ComPtr<ITexture> sampledTexture;
    ComPtr<IResourceView> sampledTextureView;
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
            {"POSITION", 0, Format::R32G32B32_FLOAT, offsetof(Vertex, position), 0},
            {"COLOR", 0, Format::R32G32B32_FLOAT, offsetof(Vertex, color), 0},
        };

        TextureDesc sampledTexDesc = {};
        sampledTexDesc.type = textureInfo->textureType;
        sampledTexDesc.numMipLevels = textureInfo->mipLevelCount;
        sampledTexDesc.arraySize = textureInfo->arrayLayerCount;
        sampledTexDesc.size = textureInfo->extents;
        sampledTexDesc.defaultState = getDefaultResourceStateForViewType(viewType);
        sampledTexDesc.allowedStates =
            ResourceStateSet(sampledTexDesc.defaultState, ResourceState::ResolveSource, ResourceState::CopySource);
        sampledTexDesc.format = textureInfo->format;
        sampledTexDesc.sampleCount = sampleCount;

        REQUIRE_CALL(
            device->createTexture(sampledTexDesc, textureInfo->subresourceDatas.data(), sampledTexture.writeRef())
        );

        TextureDesc texDesc = {};
        texDesc.type = textureInfo->textureType;
        texDesc.numMipLevels = textureInfo->mipLevelCount;
        texDesc.arraySize = textureInfo->arrayLayerCount;
        texDesc.size = textureInfo->extents;
        texDesc.defaultState = ResourceState::ResolveDestination;
        texDesc.allowedStates = ResourceStateSet(ResourceState::ResolveDestination, ResourceState::CopySource);
        texDesc.format = textureInfo->format;

        REQUIRE_CALL(device->createTexture(texDesc, textureInfo->subresourceDatas.data(), texture.writeRef()));

        InputLayoutDesc inputLayoutDesc = {};
        inputLayoutDesc.inputElementCount = SLANG_COUNT_OF(inputElements);
        inputLayoutDesc.inputElements = inputElements;
        inputLayoutDesc.vertexStreamCount = SLANG_COUNT_OF(vertexStreams);
        inputLayoutDesc.vertexStreams = vertexStreams;
        auto inputLayout = device->createInputLayout(inputLayoutDesc);
        REQUIRE(inputLayout != nullptr);

        ITransientResourceHeap::Desc transientHeapDesc = {};
        transientHeapDesc.constantBufferSize = 4096;
        REQUIRE_CALL(device->createTransientResourceHeap(transientHeapDesc, transientHeap.writeRef()));

        ComPtr<IShaderProgram> shaderProgram;
        slang::ProgramLayout* slangReflection;
        REQUIRE_CALL(loadGraphicsProgram(
            device,
            shaderProgram,
            "test-texture-types",
            "vertexMain",
            "fragmentMain",
            slangReflection
        ));

        ColorTargetState target;
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

        IResourceView::Desc colorBufferViewDesc;
        memset(&colorBufferViewDesc, 0, sizeof(colorBufferViewDesc));
        colorBufferViewDesc.format = textureInfo->format;
        colorBufferViewDesc.renderTarget.shape = textureInfo->textureType; // TODO: TextureCube?
        colorBufferViewDesc.type = viewType;
        REQUIRE_CALL(device->createTextureView(sampledTexture, colorBufferViewDesc, sampledTextureView.writeRef()));

        auto texelSize = getTexelSize(textureInfo->format);
        size_t alignment;
        device->getTextureRowAlignment(&alignment);
        alignedRowStride = (textureInfo->extents.width * texelSize + alignment - 1) & ~(alignment - 1);
    }

    void submitShaderWork(const char* entryPointName)
    {
        ICommandQueue::Desc queueDesc = {ICommandQueue::QueueType::Graphics};
        auto queue = device->createCommandQueue(queueDesc);

        auto commandBuffer = transientHeap->createCommandBuffer();

        RenderPassColorAttachment colorAttachment;
        colorAttachment.view = sampledTextureView;
        colorAttachment.loadOp = LoadOp::Clear;
        colorAttachment.storeOp = StoreOp::Store;
        colorAttachment.initialState = getDefaultResourceStateForViewType(viewType);
        colorAttachment.finalState = ResourceState::ResolveSource;
        RenderPassDesc renderPass;
        renderPass.colorAttachments = &colorAttachment;
        renderPass.colorAttachmentCount = 1;

        auto renderEncoder = commandBuffer->encodeRenderCommands(renderPass);
        auto rootObject = renderEncoder->bindPipeline(pipeline);

        Viewport viewport = {};
        // viewport.maxZ = (float)textureInfo->extents.depth;
        viewport.extentX = (float)textureInfo->extents.width;
        viewport.extentY = (float)textureInfo->extents.height;
        renderEncoder->setViewportAndScissor(viewport);

        renderEncoder->setVertexBuffer(0, vertexBuffer);
        renderEncoder->setPrimitiveTopology(PrimitiveTopology::TriangleList);
        renderEncoder->draw(kVertexCount, 0);
        renderEncoder->endEncoding();

        auto resourceEncoder = commandBuffer->encodeResourceCommands();

        if (sampleCount > 1)
        {
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

            resourceEncoder->resolveResource(
                sampledTexture,
                ResourceState::ResolveSource,
                msaaSubresource,
                texture,
                ResourceState::ResolveDestination,
                dstSubresource
            );
            resourceEncoder->textureBarrier(texture, ResourceState::ResolveDestination, ResourceState::CopySource);
        }
        else
        {
            resourceEncoder->textureBarrier(sampledTexture, ResourceState::ResolveSource, ResourceState::CopySource);
        }
        resourceEncoder->endEncoding();
        commandBuffer->close();
        queue->executeCommandBuffer(commandBuffer);
        queue->waitOnHost();
    }

    // TODO: Should take a value indicating the slice that was rendered into
    // TODO: Needs to handle either the correct slice or array layer (will not always check z)
    void validateTextureValues(ValidationTextureData actual)
    {
        for (GfxIndex x = 0; x < actual.extents.width; ++x)
        {
            for (GfxIndex y = 0; y < actual.extents.height; ++y)
            {
                for (GfxIndex z = 0; z < actual.extents.depth; ++z)
                {
                    auto actualBlock = (float*)actual.getBlockAt(x, y, z);
                    for (Int i = 0; i < 4; ++i)
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
        size_t rowPitch;
        size_t pixelSize;
        if (sampleCount > 1)
        {
            REQUIRE_CALL(
                device->readTexture(texture, ResourceState::CopySource, textureBlob.writeRef(), &rowPitch, &pixelSize)
            );
        }
        else
        {
            REQUIRE_CALL(device->readTexture(
                sampledTexture,
                ResourceState::CopySource,
                textureBlob.writeRef(),
                &rowPitch,
                &pixelSize
            ));
        }
        auto textureValues = (float*)textureBlob->getBufferPointer();

        ValidationTextureData textureResults;
        textureResults.extents = textureInfo->extents;
        textureResults.textureData = textureValues;
        textureResults.strides.x = (uint32_t)pixelSize;
        textureResults.strides.y = (uint32_t)rowPitch;
        textureResults.strides.z = textureResults.extents.height * textureResults.strides.y;

        validateTextureValues(textureResults);
    }

    void run()
    {
        auto entryPointName = getShaderEntryPoint();
        //             printf("%s\n", entryPointName.getBuffer());

        // TODO: Sampler state and null state?
        //             SamplerDesc samplerDesc;
        //             sampler = device->createSampler(samplerDesc);

        textureInfo->extents.width = 4;
        textureInfo->extents.height = (textureInfo->textureType == TextureType::Texture1D) ? 1 : 4;
        textureInfo->extents.depth = (textureInfo->textureType != TextureType::Texture3D) ? 1 : 2;
        textureInfo->mipLevelCount = 1;
        textureInfo->arrayLayerCount = 1;
        generateTextureData(textureInfo, validationFormat);

        // We need to save the pointer to the original texture data for results checking because the texture will be
        // overwritten during testing (if the texture can be written to).
        expectedTextureData = textureInfo->subresourceDatas[getSubresourceIndex(0, 1, 0)].data;

        createRequiredResources();
        submitShaderWork(entryPointName.data());

        checkTestResults();
    }
};

void testShaderAndUnordered(GpuTestContext* ctx, DeviceType deviceType)
{
    ComPtr<IDevice> device = createTestingDevice(ctx, deviceType);

    // TODO: Buffer and TextureCube
    for (Int i = 2; i < (int32_t)TextureType::TextureCube; ++i)
    {
        for (Int j = 3; j < (int32_t)IResourceView::Type::AccelerationStructure; ++j)
        {
            auto shape = (TextureType)i;
            auto view = (IResourceView::Type)j;
            auto format = Format::R8G8B8A8_UINT;
            auto validationFormat = getValidationTextureFormat(format);
            REQUIRE(validationFormat != nullptr);

            ShaderAndUnorderedTests test;
            test.init(device, format, validationFormat, view, shape);
            test.run();
        }
    }
}

void testRenderTarget(GpuTestContext* ctx, DeviceType deviceType)
{
    ComPtr<IDevice> device = createTestingDevice(ctx, deviceType);

    // TODO: Buffer and TextureCube
    for (Int i = 2; i < (int32_t)TextureType::TextureCube; ++i)
    {
        auto shape = (TextureType)i;
        auto view = IResourceView::Type::RenderTarget;
        auto format = Format::R32G32B32A32_FLOAT;
        auto validationFormat = getValidationTextureFormat(format);
        REQUIRE(validationFormat != nullptr);

        RenderTargetTests test;
        test.init(device, format, validationFormat, view, shape);
        test.run();
    }
}

TEST_CASE("texture-types-shader-and-unordered")
{
    runGpuTests(
        testShaderAndUnordered,
        {
            DeviceType::D3D12,
            DeviceType::Vulkan,
        }
    );
}

TEST_CASE("texture-types-render-target")
{
    runGpuTests(
        testRenderTarget,
        {
            DeviceType::D3D12,
            DeviceType::Vulkan,
        }
    );
}

// 1D + array + multisample, ditto for 2D, ditto for 3D
// one test with something bound, one test with nothing bound, one test with subset of layers (set values in
// SubresourceRange and assign in desc)
