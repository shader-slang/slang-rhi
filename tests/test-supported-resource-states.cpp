#include "testing.h"

#if SLANG_WINDOWS_FAMILY
#include <d3d12.h>
#endif

using namespace rhi;
using namespace rhi::testing;

struct SupportedResourceStatesTest
{
    IDevice* device;

    ResourceStateSet formatSupportedStates;
    ResourceStateSet textureAllowedStates;
    ResourceStateSet bufferAllowedStates;

    ComPtr<ITexture> texture;
    ComPtr<IBuffer> buffer;

    SupportedResourceStatesTest(IDevice* device)
        : device(device)
    {
    }

    Format convertTypelessFormat(Format format)
    {
        switch (format)
        {
        case Format::R32G32B32A32_TYPELESS:
            return Format::R32G32B32A32_FLOAT;
        case Format::R32G32B32_TYPELESS:
            return Format::R32G32B32_FLOAT;
        case Format::R32G32_TYPELESS:
            return Format::R32G32_FLOAT;
        case Format::R32_TYPELESS:
            return Format::R32_FLOAT;
        case Format::R16G16B16A16_TYPELESS:
            return Format::R16G16B16A16_FLOAT;
        case Format::R16G16_TYPELESS:
            return Format::R16G16_FLOAT;
        case Format::R16_TYPELESS:
            return Format::R16_FLOAT;
        case Format::R8G8B8A8_TYPELESS:
            return Format::R8G8B8A8_UNORM;
        case Format::R8G8_TYPELESS:
            return Format::R8G8_UNORM;
        case Format::R8_TYPELESS:
            return Format::R8_UNORM;
        case Format::B8G8R8A8_TYPELESS:
            return Format::B8G8R8A8_UNORM;
        case Format::R10G10B10A2_TYPELESS:
            return Format::R10G10B10A2_UINT;
        default:
            return Format::Unknown;
        }
    }

    void transitionResourceStates(IDevice* device)
    {
        ComPtr<ITransientResourceHeap> transientHeap;
        ITransientResourceHeap::Desc transientHeapDesc = {};
        transientHeapDesc.constantBufferSize = 4096;
        REQUIRE_CALL(device->createTransientResourceHeap(transientHeapDesc, transientHeap.writeRef()));

        ICommandQueue::Desc queueDesc = {ICommandQueue::QueueType::Graphics};
        auto queue = device->createCommandQueue(queueDesc);

        auto commandBuffer = transientHeap->createCommandBuffer();
        auto encoder = commandBuffer->encodeResourceCommands();
#if 0
        ResourceState currentTextureState = texture->getDesc()->defaultState;
        ResourceState currentBufferState = buffer->getDesc()->defaultState;
#endif

        for (uint32_t i = 0; i < (uint32_t)ResourceState::_Count; ++i)
        {
            auto nextState = (ResourceState)i;
            if (formatSupportedStates.contains(nextState))
            {
#if 0
                if (bufferAllowedStates.contains(nextState))
                {
                    encoder->bufferBarrier(buffer, currentBufferState, nextState);
                    currentBufferState = nextState;
                }
                if (textureAllowedStates.contains(nextState))
                {
                    encoder->textureBarrier(texture, currentTextureState, nextState);
                    currentTextureState = nextState;
                }
#endif
            }
        }
        encoder->endEncoding();
        commandBuffer->close();
        queue->executeCommandBuffer(commandBuffer);
        queue->waitOnHost();
    }

    void run()
    {
        // Skip Format::Unknown
        for (uint32_t i = 1; i < (uint32_t)Format::_Count; ++i)
        {
            auto baseFormat = (Format)i;
            FormatInfo info;
            rhiGetFormatInfo(baseFormat, &info);
            // Ignore 3-channel textures for now since validation layer seem to report unsupported errors there.
            if (info.channelCount == 3)
                continue;

            auto format = rhiIsTypelessFormat(baseFormat) ? convertTypelessFormat(baseFormat) : baseFormat;
            REQUIRE_CALL(device->getFormatSupportedResourceStates(format, &formatSupportedStates));

            textureAllowedStates.add(
                ResourceState::RenderTarget,
                ResourceState::DepthRead,
                ResourceState::DepthWrite,
                ResourceState::Present,
                ResourceState::ResolveSource,
                ResourceState::ResolveDestination,
                ResourceState::Undefined,
                ResourceState::ShaderResource,
                ResourceState::UnorderedAccess,
                ResourceState::CopySource,
                ResourceState::CopyDestination
            );

            bufferAllowedStates.add(
                ResourceState::VertexBuffer,
                ResourceState::IndexBuffer,
                ResourceState::ConstantBuffer,
                ResourceState::StreamOutput,
                ResourceState::IndirectArgument,
                ResourceState::AccelerationStructure,
                ResourceState::Undefined,
                ResourceState::ShaderResource,
                ResourceState::UnorderedAccess,
                ResourceState::CopySource,
                ResourceState::CopyDestination
            );

            ResourceState currentState = ResourceState::CopySource;
            Extents extent;
            extent.width = 4;
            extent.height = 4;
            extent.depth = 1;

#if 0
            TextureDesc texDesc = {};
            texDesc.type = TextureType::Texture2D;
            texDesc.numMipLevels = 1;
            texDesc.arraySize = 1;
            texDesc.size = extent;
            texDesc.usage = formatSupportedStates & textureAllowedStates;
            texDesc.defaultState = currentState;
            texDesc.memoryType = MemoryType::DeviceLocal;
            texDesc.format = format;

            REQUIRE_CALL(device->createTexture(texDesc, nullptr, texture.writeRef()));

            BufferDesc bufferDesc = {};
            bufferDesc.size = 256;
            bufferDesc.format = Format::Unknown;
            bufferDesc.elementSize = sizeof(float);
            bufferDesc.allowedStates = formatSupportedStates & bufferAllowedStates;
            bufferDesc.defaultState = currentState;
            bufferDesc.memoryType = MemoryType::DeviceLocal;

            REQUIRE_CALL(device->createBuffer(bufferDesc, nullptr, buffer.writeRef()));
#endif

            transitionResourceStates(device);
        }
    }
};

void testSupportedResourceStates(GpuTestContext* ctx, DeviceType deviceType)
{
    ComPtr<IDevice> device = createTestingDevice(ctx, deviceType);
    SupportedResourceStatesTest test(device);
    test.run();
}

TEST_CASE("supported-resource-states")
{
    runGpuTests(
        testSupportedResourceStates,
        {
            DeviceType::D3D12,
            DeviceType::Vulkan,
        }
    );
}
