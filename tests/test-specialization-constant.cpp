#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

// Specialization constants take their defaults when the pipeline sets none. On Metal they are
// function constants, and a function that declares any cannot build a pipeline until it is
// specialized.

// Only the backends with native specialization constants. On D3D11, D3D12, CUDA and CPU Slang lays a
// specialization constant out as an ordinary uniform, which reads 0 unless the host writes the
// default from reflection.
GPU_TEST_CASE("specialization-constant-compute", Vulkan | Metal | WGPU)
{
    ComPtr<IShaderProgram> shaderProgram;
    REQUIRE_CALL(loadProgram(device, "test-specialization-constant", "computeMain", shaderProgram.writeRef()));

    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

    float initialData[] = {0.0f, 0.0f, 0.0f, 0.0f};
    BufferDesc bufferDesc = {};
    bufferDesc.size = sizeof(initialData);
    bufferDesc.elementSize = sizeof(float);
    bufferDesc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess | BufferUsage::CopyDestination |
                       BufferUsage::CopySource;
    bufferDesc.defaultState = ResourceState::UnorderedAccess;
    ComPtr<IBuffer> buffer;
    REQUIRE_CALL(device->createBuffer(bufferDesc, initialData, buffer.writeRef()));

    {
        auto queue = device->getQueue(QueueType::Graphics);
        auto commandEncoder = queue->createCommandEncoder();
        auto passEncoder = commandEncoder->beginComputePass();
        auto rootObject = passEncoder->bindPipeline(pipeline);
        ShaderCursor(rootObject)["buffer"].setBinding(buffer);
        passEncoder->dispatchCompute(1, 1, 1);
        passEncoder->end();
        queue->submit(commandEncoder->finish());
        queue->waitOnHost();
    }

    compareComputeResult(device, buffer, makeArray<float>(1.0f, -7.0f, 9.0f, 2.5f));
}

GPU_TEST_CASE("specialization-constant-render", Vulkan | Metal | WGPU)
{
    ComPtr<IShaderProgram> shaderProgram;
    REQUIRE_CALL(
        loadProgram(device, "test-specialization-constant", {"vertexMain", "fragmentMain"}, shaderProgram.writeRef())
    );

    const Format format = Format::RGBA32Float;
    ColorTargetDesc colorTarget;
    colorTarget.format = format;
    RenderPipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    pipelineDesc.targets = &colorTarget;
    pipelineDesc.targetCount = 1;
    pipelineDesc.depthStencil.depthTestEnable = false;
    pipelineDesc.depthStencil.depthWriteEnable = false;
    ComPtr<IRenderPipeline> pipeline;
    REQUIRE_CALL(device->createRenderPipeline(pipelineDesc, pipeline.writeRef()));

    const uint32_t size = 4;
    TextureDesc textureDesc = {};
    textureDesc.type = TextureType::Texture2D;
    textureDesc.size = {size, size, 1};
    textureDesc.format = format;
    textureDesc.usage = TextureUsage::RenderTarget | TextureUsage::CopySource;
    textureDesc.defaultState = ResourceState::RenderTarget;
    ComPtr<ITexture> texture;
    REQUIRE_CALL(device->createTexture(textureDesc, nullptr, texture.writeRef()));
    ComPtr<ITextureView> view;
    TextureViewDesc viewDesc = {};
    viewDesc.format = format;
    REQUIRE_CALL(device->createTextureView(texture, viewDesc, view.writeRef()));

    {
        auto queue = device->getQueue(QueueType::Graphics);
        auto commandEncoder = queue->createCommandEncoder();
        RenderPassColorAttachment colorAttachment;
        colorAttachment.view = view;
        colorAttachment.loadOp = LoadOp::Clear;
        colorAttachment.storeOp = StoreOp::Store;
        RenderPassDesc renderPass;
        renderPass.colorAttachments = &colorAttachment;
        renderPass.colorAttachmentCount = 1;
        auto passEncoder = commandEncoder->beginRenderPass(renderPass);
        passEncoder->bindPipeline(pipeline);
        RenderState state;
        state.viewports[0] = Viewport::fromSize(size, size);
        state.viewportCount = 1;
        state.scissorRects[0] = ScissorRect::fromSize(size, size);
        state.scissorRectCount = 1;
        passEncoder->setRenderState(state);
        DrawArguments args;
        args.vertexCount = 3;
        passEncoder->draw(args);
        passEncoder->end();
        queue->submit(commandEncoder->finish());
        queue->waitOnHost();
    }

    ComPtr<ISlangBlob> blob;
    SubresourceLayout layout;
    REQUIRE_CALL(device->readTexture(texture, 0, 0, blob.writeRef(), &layout));
    const float* pixel =
        (const float*)((const uint8_t*)blob->getBufferPointer() + layout.rowPitch * (size / 2)) + 4 * (size / 2);
    float expected[] = {1.0f, -7.0f, 9.0f, 2.5f};
    compareResultFuzzy(pixel, expected, 4);
}
