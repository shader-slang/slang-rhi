#include "testing.h"

#include <set>

using namespace rhi;
using namespace rhi::testing;

void testBufferResourceStates(GpuTestContext* ctx, DeviceType deviceType)
{
    ComPtr<IDevice> device = createTestingDevice(ctx, deviceType);

    ComPtr<ITransientResourceHeap> transientHeap;
    ITransientResourceHeap::Desc transientHeapDesc = {};
    transientHeapDesc.constantBufferSize = 4096;
    REQUIRE_CALL(device->createTransientResourceHeap(transientHeapDesc, transientHeap.writeRef()));

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
    bufferDesc.format = Format::Unknown;
    bufferDesc.elementSize = sizeof(float);
    bufferDesc.usage = bufferUsage;
    bufferDesc.memoryType = MemoryType::DeviceLocal;
    ComPtr<IBuffer> buffer;
    REQUIRE_CALL(device->createBuffer(bufferDesc, nullptr, buffer.writeRef()));

    for (ResourceState state : allowedStates)
    {
        auto commandBuffer = transientHeap->createCommandBuffer();
        auto passEncoder = commandBuffer->beginResourcePass();
        passEncoder->setBufferState(buffer, state);
        passEncoder->end();
        commandBuffer->close();
        queue->submit(commandBuffer);
    }

    queue->waitOnHost();
}

void testTextureResourceStates(GpuTestContext* ctx, DeviceType deviceType)
{
    ComPtr<IDevice> device = createTestingDevice(ctx, deviceType);

    ComPtr<ITransientResourceHeap> transientHeap;
    ITransientResourceHeap::Desc transientHeapDesc = {};
    transientHeapDesc.constantBufferSize = 4096;
    REQUIRE_CALL(device->createTransientResourceHeap(transientHeapDesc, transientHeap.writeRef()));

    auto queue = device->getQueue(QueueType::Graphics);

    for (uint32_t i = 1; i < (uint32_t)Format::_Count; ++i)
    {
        auto format = (Format)i;
        const FormatInfo& info = getFormatInfo(format);
        FormatSupport formatSupport;
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
            textureUsage |= (TextureUsage::DepthRead | TextureUsage::DepthWrite);
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
        texDesc.size = Extents{4, 4, 1};
        texDesc.mipLevelCount = 1;
        texDesc.usage = textureUsage;
        texDesc.memoryType = MemoryType::DeviceLocal;
        ComPtr<ITexture> texture;
        REQUIRE_CALL(device->createTexture(texDesc, nullptr, texture.writeRef()));

        for (ResourceState state : allowedStates)
        {
            auto commandBuffer = transientHeap->createCommandBuffer();
            auto passEncoder = commandBuffer->beginResourcePass();
            passEncoder->setTextureState(texture, state);
            passEncoder->end();
            commandBuffer->close();
            queue->submit(commandBuffer);
        }

        queue->waitOnHost();
    }
}

TEST_CASE("buffer-resource-states")
{
    runGpuTests(
        testBufferResourceStates,
        {
            DeviceType::D3D12,
            DeviceType::Vulkan,
        }
    );
}

TEST_CASE("texture-resource-states")
{
    runGpuTests(
        testTextureResourceStates,
        {
            DeviceType::D3D12,
            DeviceType::Vulkan,
        }
    );
}
