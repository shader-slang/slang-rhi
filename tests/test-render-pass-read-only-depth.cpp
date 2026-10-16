#include "testing.h"

#include <vector>

using namespace rhi;
using namespace rhi::testing;

// A read-only depth attachment keeps the caller's load/store ops in the shared desc. The pass must
// still be valid (WebGPU requires undefined ops on read-only aspects), so its color clear lands.
// D3D12 is excluded: it binds a writable DSV while the depth texture is in the DepthRead state.
GPU_TEST_CASE("render-pass-read-only-depth", Vulkan | Metal | WGPU)
{
    const uint32_t size = 4;

    TextureDesc colorDesc = {};
    colorDesc.type = TextureType::Texture2D;
    colorDesc.format = Format::RGBA32Float;
    colorDesc.size = {size, size, 1};
    colorDesc.usage = TextureUsage::RenderTarget | TextureUsage::CopySource;
    colorDesc.defaultState = ResourceState::RenderTarget;
    ComPtr<ITexture> color;
    REQUIRE_CALL(device->createTexture(colorDesc, nullptr, color.writeRef()));
    ComPtr<ITextureView> colorView;
    REQUIRE_CALL(device->createTextureView(color, {}, colorView.writeRef()));

    TextureDesc depthDesc = {};
    depthDesc.type = TextureType::Texture2D;
    depthDesc.format = Format::D32Float;
    depthDesc.size = {size, size, 1};
    depthDesc.usage = TextureUsage::DepthStencil | TextureUsage::ShaderResource;
    depthDesc.defaultState = ResourceState::DepthWrite;
    ComPtr<ITexture> depth;
    REQUIRE_CALL(device->createTexture(depthDesc, nullptr, depth.writeRef()));
    ComPtr<ITextureView> depthView;
    REQUIRE_CALL(device->createTextureView(depth, {}, depthView.writeRef()));

    ComPtr<ICommandQueue> queue = device->getQueue(QueueType::Graphics);
    ComPtr<ICommandEncoder> encoder = queue->createCommandEncoder();

    // Initialize depth in a writable pass first.
    {
        RenderPassDepthStencilAttachment depthAttachment = {};
        depthAttachment.view = depthView;
        RenderPassDesc pass = {};
        pass.depthStencilAttachment = &depthAttachment;
        encoder->beginRenderPass(pass)->end();
    }

    {
        RenderPassColorAttachment colorAttachment = {};
        colorAttachment.view = colorView;
        colorAttachment.loadOp = LoadOp::Clear;
        colorAttachment.clearValue[0] = 0.25f;
        colorAttachment.clearValue[1] = 0.5f;
        colorAttachment.clearValue[2] = 0.75f;
        colorAttachment.clearValue[3] = 1.f;
        RenderPassDepthStencilAttachment depthAttachment = {};
        depthAttachment.view = depthView;
        depthAttachment.depthLoadOp = LoadOp::Load;
        depthAttachment.depthStoreOp = StoreOp::Store;
        depthAttachment.depthReadOnly = true;
        RenderPassDesc pass = {};
        pass.colorAttachments = &colorAttachment;
        pass.colorAttachmentCount = 1;
        pass.depthStencilAttachment = &depthAttachment;
        encoder->beginRenderPass(pass)->end();
    }
    queue->submit(encoder->finish());
    queue->waitOnHost();

    ComPtr<ISlangBlob> blob;
    SubresourceLayout layout;
    REQUIRE_CALL(device->readTexture(color, 0, 0, blob.writeRef(), &layout));
    const float expected[4] = {0.25f, 0.5f, 0.75f, 1.f};
    bool equal = true;
    for (uint32_t y = 0; y < layout.size.height; ++y)
    {
        const float* row =
            reinterpret_cast<const float*>(static_cast<const uint8_t*>(blob->getBufferPointer()) + y * layout.rowPitch);
        for (uint32_t x = 0; x < layout.size.width * 4; ++x)
            equal &= row[x] == expected[x % 4];
    }
    CHECK(equal);
}
