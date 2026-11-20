#include "testing.h"
#include "../src/state-tracking.h"

#include <set>

using namespace rhi;
using namespace rhi::testing;

GPU_TEST_CASE("buffer-resource-states", D3D12 | Vulkan)
{
    ComPtr<IShaderProgram> shaderProgram;
    REQUIRE_CALL(loadProgram(device, "test-dummy", "computeMain", shaderProgram.writeRef()));

    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

    auto queue = device->getQueue(QueueType::Graphics);

    BufferUsage bufferUsage = BufferUsage::VertexBuffer | BufferUsage::IndexBuffer | BufferUsage::ConstantBuffer |
                              BufferUsage::ShaderResource | BufferUsage::UnorderedAccess |
                              BufferUsage::IndirectArgument | BufferUsage::CopySource | BufferUsage::CopyDestination |
                              //   BufferUsage::AccelerationStructure | BufferUsage::AccelerationStructureBuildInput |
                              BufferUsage::ShaderTable;

    std::set<ResourceState> allowedStates = {
        ResourceState::VertexBuffer,
        ResourceState::IndexBuffer,
        ResourceState::ConstantBuffer,
        ResourceState::ShaderResource,
        ResourceState::UnorderedAccess,
        ResourceState::IndirectArgument,
        ResourceState::CopySource,
        ResourceState::CopyDestination,
        // ResourceState::AccelerationStructure,
        // ResourceState::AccelerationStructureBuildInput,
    };

    BufferDesc bufferDesc = {};
    bufferDesc.size = 256;
    bufferDesc.format = Format::Undefined;
    bufferDesc.elementSize = sizeof(float);
    bufferDesc.usage = bufferUsage;
    bufferDesc.memoryType = MemoryType::DeviceLocal;
    ComPtr<IBuffer> buffer;
    REQUIRE_CALL(device->createBuffer(bufferDesc, nullptr, buffer.writeRef()));

    for (ResourceState state : allowedStates)
    {
        auto commandEncoder = queue->createCommandEncoder();
        commandEncoder->setBufferState(buffer, state);
        auto passEncoder = commandEncoder->beginComputePass();
        passEncoder->bindPipeline(pipeline);
        passEncoder->dispatchCompute(1, 1, 1);
        passEncoder->end();
        queue->submit(commandEncoder->finish());
    }

    queue->waitOnHost();
}

GPU_TEST_CASE("texture-resource-states", D3D12 | Vulkan)
{
    ComPtr<IShaderProgram> shaderProgram;
    REQUIRE_CALL(loadProgram(device, "test-dummy", "computeMain", shaderProgram.writeRef()));

    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

    auto queue = device->getQueue(QueueType::Graphics);

    for (uint32_t i = 1; i < (uint32_t)Format::_Count; ++i)
    {
        auto format = (Format)i;
        FormatSupport formatSupport = FormatSupport::None;
        REQUIRE_CALL(device->getFormatSupport(format, &formatSupport));

        if (!is_set(formatSupport, FormatSupport::Texture))
            continue;

        TextureUsage textureUsage = TextureUsage::CopySource | TextureUsage::CopyDestination |
                                    TextureUsage::ResolveSource | TextureUsage::ResolveDestination;

        std::set<ResourceState> allowedStates = {
            ResourceState::ResolveSource,
            ResourceState::ResolveDestination,
            ResourceState::CopySource,
            ResourceState::CopyDestination
        };

        if (is_set(formatSupport, FormatSupport::RenderTarget))
        {
            textureUsage |= TextureUsage::RenderTarget;
            allowedStates.insert(ResourceState::RenderTarget);
        }
        if (is_set(formatSupport, FormatSupport::DepthStencil))
        {
            textureUsage |= (TextureUsage::DepthStencil);
            allowedStates.insert(ResourceState::DepthRead);
            allowedStates.insert(ResourceState::DepthWrite);
        }
        if (is_set(formatSupport, FormatSupport::ShaderLoad) || is_set(formatSupport, FormatSupport::ShaderSample))
        {
            textureUsage |= TextureUsage::ShaderResource;
            allowedStates.insert(ResourceState::ShaderResource);
        }
        if (is_set(formatSupport, FormatSupport::ShaderUavLoad) || is_set(formatSupport, FormatSupport::ShaderUavStore))
        {
            textureUsage |= TextureUsage::UnorderedAccess;
            allowedStates.insert(ResourceState::UnorderedAccess);
        }

        TextureDesc texDesc = {};
        texDesc.type = TextureType::Texture2D;
        texDesc.format = format;
        texDesc.size = Extent3D{4, 4, 1};
        texDesc.mipCount = 1;
        texDesc.usage = textureUsage;
        texDesc.memoryType = MemoryType::DeviceLocal;
        ComPtr<ITexture> texture;
        REQUIRE_CALL(device->createTexture(texDesc, nullptr, texture.writeRef()));

        for (ResourceState state : allowedStates)
        {
            auto commandEncoder = queue->createCommandEncoder();
            commandEncoder->setTextureState(texture, state);
            auto passEncoder = commandEncoder->beginComputePass();
            passEncoder->bindPipeline(pipeline);
            passEncoder->dispatchCompute(1, 1, 1);
            passEncoder->end();
            queue->submit(commandEncoder->finish());
        }

        queue->waitOnHost();
    }
}

// Changing the state of a layer range must only affect the layers in that range.
GPU_TEST_CASE("texture-state-tracking-layer-range", D3D11 | D3D12 | Vulkan | Metal | WGPU)
{
    const uint32_t kLayerCount = 4;
    const uint32_t kMipCount = 2;

    TextureDesc textureDesc = {};
    textureDesc.type = TextureType::Texture2DArray;
    textureDesc.format = Format::RGBA8Unorm;
    textureDesc.size = Extent3D{4, 4, 1};
    textureDesc.arrayLength = kLayerCount;
    textureDesc.mipCount = kMipCount;
    textureDesc.usage = TextureUsage::ShaderResource | TextureUsage::CopySource | TextureUsage::CopyDestination;
    ComPtr<ITexture> texture;
    REQUIRE_CALL(device->createTexture(textureDesc, nullptr, texture.writeRef()));
    Texture* textureImpl = checked_cast<Texture*>(texture.get());

    StateTracking stateTracking;

    // Transition all mips of layers 1 and 2.
    stateTracking.setTextureState(textureImpl, SubresourceRange{1, 2, 0, kMipCount}, ResourceState::CopySource);
    CHECK(stateTracking.getTextureBarriers().size() == 2 * kMipCount);
    for (const TextureBarrier& barrier : stateTracking.getTextureBarriers())
    {
        CHECK(!barrier.entireTexture);
        CHECK(barrier.layer >= 1);
        CHECK(barrier.layer <= 2);
        CHECK(barrier.stateAfter == ResourceState::CopySource);
    }
    stateTracking.clearBarriers();

    // Transition a single mip of layer 0. This must not touch the layers transitioned above.
    stateTracking.setTextureState(textureImpl, SubresourceRange{0, 1, 1, 1}, ResourceState::CopyDestination);
    REQUIRE(stateTracking.getTextureBarriers().size() == 1);
    CHECK(stateTracking.getTextureBarriers()[0].layer == 0);
    CHECK(stateTracking.getTextureBarriers()[0].mip == 1);
    stateTracking.clearBarriers();

    // Layers 1 and 2 are still in the state set above, so transitioning them again is a no-op.
    stateTracking.setTextureState(textureImpl, SubresourceRange{1, 2, 0, kMipCount}, ResourceState::CopySource);
    CHECK(stateTracking.getTextureBarriers().empty());
}
