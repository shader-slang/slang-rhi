#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

// Bindless buffers are currently not supported on CUDA.
GPU_TEST_CASE("bindless-buffers", D3D12 | Vulkan)
{
    if (!device->hasFeature(Feature::Bindless))
    {
        SKIP("Bindless is not supported");
    }

    ComPtr<IShaderProgram> shaderProgram;
    REQUIRE_CALL(loadProgram(device, "test-bindless-buffers", "computeMain", shaderProgram.writeRef()));

    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

    ComPtr<IBuffer> buffer;
    DescriptorHandle bufferHandle = {};
    {
        BufferDesc desc = {};
        desc.format = Format::R32Float;
        desc.size = 8;
        desc.usage = BufferUsage::ShaderResource;
        float data[2] = {1.f, 2.f};
        REQUIRE_CALL(device->createBuffer(desc, data, buffer.writeRef()));
        REQUIRE_CALL(
            buffer->getDescriptorHandle(DescriptorHandleAccess::Read, Format::R32Float, kEntireBuffer, &bufferHandle)
        );
        CHECK(bufferHandle.type == DescriptorHandleType::Buffer);
    }

    ComPtr<IBuffer> structuredBuffer;
    DescriptorHandle structuredBufferHandle = {};
    {
        BufferDesc desc = {};
        desc.size = 8;
        desc.usage = BufferUsage::ShaderResource;
        float data[2] = {1.f, 2.f};
        REQUIRE_CALL(device->createBuffer(desc, data, structuredBuffer.writeRef()));
        REQUIRE_CALL(structuredBuffer->getDescriptorHandle(
            DescriptorHandleAccess::Read,
            Format::Undefined,
            kEntireBuffer,
            &structuredBufferHandle
        ));
        CHECK(structuredBufferHandle.type == DescriptorHandleType::Buffer);
    }

    ComPtr<IBuffer> byteAddressBuffer;
    DescriptorHandle byteAddressBufferHandle = {};
    {
        BufferDesc desc = {};
        desc.size = 8;
        desc.usage = BufferUsage::ShaderResource;
        float data[2] = {1.f, 2.f};
        REQUIRE_CALL(device->createBuffer(desc, data, byteAddressBuffer.writeRef()));
        REQUIRE_CALL(byteAddressBuffer->getDescriptorHandle(
            DescriptorHandleAccess::Read,
            Format::Undefined,
            kEntireBuffer,
            &byteAddressBufferHandle
        ));
        CHECK(byteAddressBufferHandle.type == DescriptorHandleType::Buffer);
    }

    ComPtr<IBuffer> rwBuffer;
    DescriptorHandle rwBufferHandle = {};
    {
        BufferDesc desc = {};
        desc.format = Format::R32Float;
        desc.size = 8;
        desc.usage = BufferUsage::UnorderedAccess | BufferUsage::CopySource;
        float data[2] = {1.f, 2.f};
        REQUIRE_CALL(device->createBuffer(desc, data, rwBuffer.writeRef()));
        REQUIRE_CALL(rwBuffer->getDescriptorHandle(
            DescriptorHandleAccess::ReadWrite,
            Format::R32Float,
            kEntireBuffer,
            &rwBufferHandle
        ));
        CHECK(rwBufferHandle.type == DescriptorHandleType::RWBuffer);
    }

    ComPtr<IBuffer> rwStructuredBuffer;
    DescriptorHandle rwStructuredBufferHandle = {};
    {
        BufferDesc desc = {};
        desc.format = Format::R32Float;
        desc.size = 8;
        desc.usage = BufferUsage::UnorderedAccess | BufferUsage::CopySource;
        float data[2] = {1.f, 2.f};
        REQUIRE_CALL(device->createBuffer(desc, data, rwStructuredBuffer.writeRef()));
        REQUIRE_CALL(rwStructuredBuffer->getDescriptorHandle(
            DescriptorHandleAccess::ReadWrite,
            Format::Undefined,
            kEntireBuffer,
            &rwStructuredBufferHandle
        ));
        CHECK(rwStructuredBufferHandle.type == DescriptorHandleType::RWBuffer);
    }

    ComPtr<IBuffer> rwByteAddressBuffer;
    DescriptorHandle rwByteAddressBufferHandle = {};
    {
        BufferDesc desc = {};
        desc.format = Format::R32Float;
        desc.size = 8;
        desc.usage = BufferUsage::UnorderedAccess | BufferUsage::CopySource;
        float data[2] = {1.f, 2.f};
        REQUIRE_CALL(device->createBuffer(desc, data, rwByteAddressBuffer.writeRef()));
        REQUIRE_CALL(rwByteAddressBuffer->getDescriptorHandle(
            DescriptorHandleAccess::ReadWrite,
            Format::Undefined,
            kEntireBuffer,
            &rwByteAddressBufferHandle
        ));
        CHECK(rwByteAddressBufferHandle.type == DescriptorHandleType::RWBuffer);
    }

    ComPtr<IBuffer> result;
    {
        BufferDesc desc = {};
        desc.size = 1024;
        desc.usage = BufferUsage::UnorderedAccess | BufferUsage::CopySource;
        REQUIRE_CALL(device->createBuffer(desc, nullptr, result.writeRef()));
    }

    {
        auto queue = device->getQueue(QueueType::Graphics);
        auto commandEncoder = queue->createCommandEncoder();
        auto passEncoder = commandEncoder->beginComputePass();
        IShaderObject* rootObject = passEncoder->bindPipeline(pipeline);
        ShaderCursor cursor(rootObject);
        cursor["buffer"].setDescriptorHandle(bufferHandle);
        cursor["structuredBuffer"].setDescriptorHandle(structuredBufferHandle);
        cursor["byteAddressBuffer"].setDescriptorHandle(byteAddressBufferHandle);
        cursor["rwBuffer"].setDescriptorHandle(rwBufferHandle);
        cursor["rwStructuredBuffer"].setDescriptorHandle(rwStructuredBufferHandle);
        cursor["rwByteAddressBuffer"].setDescriptorHandle(rwByteAddressBufferHandle);
        cursor["result"].setBinding(result);

        passEncoder->dispatchCompute(1, 1, 1);
        passEncoder->end();

        queue->submit(commandEncoder->finish());
        queue->waitOnHost();
    }

    compareComputeResult(
        device,
        result,
        std::array{
            // Buffer
            1.f,
            2.f,
            // StructuredBuffer
            1.f,
            2.f,
            // ByteAddressBuffer
            1.f,
            2.f,
            // RWBuffer
            1.f,
            2.f,
            // RWStructuredBuffer
            1.f,
            2.f,
            // RWByteAddressBuffer
            1.f,
            2.f,
        }
    );

    compareComputeResult(device, rwBuffer, std::array{2.f, 3.f});
    compareComputeResult(device, rwStructuredBuffer, std::array{2.f, 3.f});
    compareComputeResult(device, rwByteAddressBuffer, std::array{2.f, 3.f});
}

GPU_TEST_CASE("bindless-textures", D3D12 | Vulkan | CUDA)
{
    if (!device->hasFeature(Feature::Bindless))
    {
        SKIP("Bindless is not supported");
    }

    ComPtr<IShaderProgram> shaderProgram;
    REQUIRE_CALL(loadProgram(device, "test-bindless-textures", "computeMain", shaderProgram.writeRef()));

    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

    ComPtr<ITexture> texture1D;
    ComPtr<ITextureView> texture1DView;
    DescriptorHandle texture1DHandle = {};
    {
        TextureDesc desc = {};
        desc.type = TextureType::Texture1D;
        desc.size = {2, 1, 1};
        desc.format = Format::R32Float;
        desc.usage = TextureUsage::ShaderResource;
        float data[2] = {1.f, 2.f};
        SubresourceData subresourceData[] = {{data, 8, 0}};
        REQUIRE_CALL(device->createTexture(desc, subresourceData, texture1D.writeRef()));
        REQUIRE_CALL(texture1D->createView({}, texture1DView.writeRef()));
        REQUIRE_CALL(texture1DView->getDescriptorHandle(DescriptorHandleAccess::Read, &texture1DHandle));
        CHECK(texture1DHandle.type == DescriptorHandleType::Texture);
    }

    ComPtr<ITexture> texture2D;
    ComPtr<ITextureView> texture2DView;
    DescriptorHandle texture2DHandle = {};
    {
        TextureDesc desc = {};
        desc.type = TextureType::Texture2D;
        desc.size = {2, 2, 1};
        desc.format = Format::R32Float;
        desc.usage = TextureUsage::ShaderResource;
        float data[4] = {1.f, 2.f, 3.f, 4.f};
        SubresourceData subresourceData[] = {{data, 8, 0}};
        REQUIRE_CALL(device->createTexture(desc, subresourceData, texture2D.writeRef()));
        REQUIRE_CALL(texture2D->createView({}, texture2DView.writeRef()));
        REQUIRE_CALL(texture2DView->getDescriptorHandle(DescriptorHandleAccess::Read, &texture2DHandle));
        CHECK(texture2DHandle.type == DescriptorHandleType::Texture);
    }

    ComPtr<ITexture> texture3D;
    ComPtr<ITextureView> texture3DView;
    DescriptorHandle texture3DHandle = {};
    {
        TextureDesc desc = {};
        desc.type = TextureType::Texture3D;
        desc.size = {2, 2, 2};
        desc.format = Format::R32Float;
        desc.usage = TextureUsage::ShaderResource;
        float data[8] = {1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 7.f, 8.f};
        SubresourceData subresourceData[] = {{data, 8, 16}};
        REQUIRE_CALL(device->createTexture(desc, subresourceData, texture3D.writeRef()));
        REQUIRE_CALL(texture3D->createView({}, texture3DView.writeRef()));
        REQUIRE_CALL(texture3DView->getDescriptorHandle(DescriptorHandleAccess::Read, &texture3DHandle));
        CHECK(texture3DHandle.type == DescriptorHandleType::Texture);
    }

    ComPtr<ITexture> textureCube;
    ComPtr<ITextureView> textureCubeView;
    DescriptorHandle textureCubeHandle = {};
    {
        TextureDesc desc = {};
        desc.type = TextureType::TextureCube;
        desc.size = {1, 1, 1};
        desc.format = Format::R32Float;
        desc.usage = TextureUsage::ShaderResource;
        float data[6][1] = {{1.f}, {2.f}, {3.f}, {4.f}, {5.f}, {6.f}};
        SubresourceData subresourceData[] =
            {{data[0], 4, 0}, {data[1], 4, 0}, {data[2], 4, 0}, {data[3], 4, 0}, {data[4], 4, 0}, {data[5], 4, 0}};
        REQUIRE_CALL(device->createTexture(desc, subresourceData, textureCube.writeRef()));
        REQUIRE_CALL(textureCube->createView({}, textureCubeView.writeRef()));
        REQUIRE_CALL(textureCubeView->getDescriptorHandle(DescriptorHandleAccess::Read, &textureCubeHandle));
        CHECK(textureCubeHandle.type == DescriptorHandleType::Texture);
    }

    ComPtr<ITexture> texture1DArray;
    ComPtr<ITextureView> texture1DArrayView;
    DescriptorHandle texture1DArrayHandle = {};
    {
        TextureDesc desc = {};
        desc.type = TextureType::Texture1DArray;
        desc.size = {2, 1, 1};
        desc.arrayLength = 2;
        desc.format = Format::R32Float;
        desc.usage = TextureUsage::ShaderResource;
        float data[2][2] = {{1.f, 2.f}, {3.f, 4.f}};
        SubresourceData subresourceData[] = {{data[0], 8, 0}, {data[1], 8, 0}};
        REQUIRE_CALL(device->createTexture(desc, subresourceData, texture1DArray.writeRef()));
        REQUIRE_CALL(texture1DArray->createView({}, texture1DArrayView.writeRef()));
        REQUIRE_CALL(texture1DArrayView->getDescriptorHandle(DescriptorHandleAccess::Read, &texture1DArrayHandle));
        CHECK(texture1DArrayHandle.type == DescriptorHandleType::Texture);
    }

    ComPtr<ITexture> texture2DArray;
    ComPtr<ITextureView> texture2DArrayView;
    DescriptorHandle texture2DArrayHandle = {};
    {
        TextureDesc desc = {};
        desc.type = TextureType::Texture2DArray;
        desc.size = {2, 2, 1};
        desc.arrayLength = 2;
        desc.format = Format::R32Float;
        desc.usage = TextureUsage::ShaderResource;
        float data[2][4] = {{1.f, 2.f, 3.f, 4.f}, {5.f, 6.f, 7.f, 8.f}};
        SubresourceData subresourceData[] = {{data[0], 8, 0}, {data[1], 8, 0}};
        REQUIRE_CALL(device->createTexture(desc, subresourceData, texture2DArray.writeRef()));
        REQUIRE_CALL(texture2DArray->createView({}, texture2DArrayView.writeRef()));
        REQUIRE_CALL(texture2DArrayView->getDescriptorHandle(DescriptorHandleAccess::Read, &texture2DArrayHandle));
        CHECK(texture2DArrayHandle.type == DescriptorHandleType::Texture);
    }

    ComPtr<ITexture> textureCubeArray;
    ComPtr<ITextureView> textureCubeArrayView;
    DescriptorHandle textureCubeArrayHandle = {};
    {
        TextureDesc desc = {};
        desc.type = TextureType::TextureCubeArray;
        desc.size = {1, 1, 1};
        desc.arrayLength = 2;
        desc.format = Format::R32Float;
        desc.usage = TextureUsage::ShaderResource;
        float data[12][1] = {{1.f}, {2.f}, {3.f}, {4.f}, {5.f}, {6.f}, {7.f}, {8.f}, {9.f}, {10.f}, {11.f}, {12.f}};
        SubresourceData subresourceData[] = {
            {data[0], 4, 0},
            {data[1], 4, 0},
            {data[2], 4, 0},
            {data[3], 4, 0},
            {data[4], 4, 0},
            {data[5], 4, 0},
            {data[6], 4, 0},
            {data[7], 4, 0},
            {data[8], 4, 0},
            {data[9], 4, 0},
            {data[10], 4, 0},
            {data[11], 4, 0}
        };
        REQUIRE_CALL(device->createTexture(desc, subresourceData, textureCubeArray.writeRef()));
        REQUIRE_CALL(textureCubeArray->createView({}, textureCubeArrayView.writeRef()));
        REQUIRE_CALL(textureCubeArrayView->getDescriptorHandle(DescriptorHandleAccess::Read, &textureCubeArrayHandle));
        CHECK(textureCubeArrayHandle.type == DescriptorHandleType::Texture);
    }

    ComPtr<ITexture> rwTexture1D;
    ComPtr<ITextureView> rwTexture1DView;
    DescriptorHandle rwTexture1DHandle = {};
    {
        TextureDesc desc = {};
        desc.type = TextureType::Texture1D;
        desc.size = {2, 1, 1};
        desc.format = Format::R32Float;
        desc.usage = TextureUsage::UnorderedAccess | TextureUsage::CopySource;
        float data[2] = {1.f, 2.f};
        SubresourceData subresourceData[] = {{data, 8, 0}};
        REQUIRE_CALL(device->createTexture(desc, subresourceData, rwTexture1D.writeRef()));
        REQUIRE_CALL(rwTexture1D->createView({}, rwTexture1DView.writeRef()));
        REQUIRE_CALL(rwTexture1DView->getDescriptorHandle(DescriptorHandleAccess::ReadWrite, &rwTexture1DHandle));
        CHECK(rwTexture1DHandle.type == DescriptorHandleType::RWTexture);
    }

    ComPtr<ITexture> rwTexture2D;
    ComPtr<ITextureView> rwTexture2DView;
    DescriptorHandle rwTexture2DHandle = {};
    {
        TextureDesc desc = {};
        desc.type = TextureType::Texture2D;
        desc.size = {2, 2, 1};
        desc.format = Format::R32Float;
        desc.usage = TextureUsage::UnorderedAccess | TextureUsage::CopySource;
        ;
        float data[4] = {1.f, 2.f, 3.f, 4.f};
        SubresourceData subresourceData[] = {{data, 8, 0}};
        REQUIRE_CALL(device->createTexture(desc, subresourceData, rwTexture2D.writeRef()));
        REQUIRE_CALL(rwTexture2D->createView({}, rwTexture2DView.writeRef()));
        REQUIRE_CALL(rwTexture2DView->getDescriptorHandle(DescriptorHandleAccess::ReadWrite, &rwTexture2DHandle));
        CHECK(rwTexture2DHandle.type == DescriptorHandleType::RWTexture);
    }

    ComPtr<ITexture> rwTexture3D;
    ComPtr<ITextureView> rwTexture3DView;
    DescriptorHandle rwTexture3DHandle = {};
    {
        TextureDesc desc = {};
        desc.type = TextureType::Texture3D;
        desc.size = {2, 2, 2};
        desc.format = Format::R32Float;
        desc.usage = TextureUsage::UnorderedAccess | TextureUsage::CopySource;
        ;
        float data[8] = {1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 7.f, 8.f};
        SubresourceData subresourceData[] = {{data, 8, 16}};
        REQUIRE_CALL(device->createTexture(desc, subresourceData, rwTexture3D.writeRef()));
        REQUIRE_CALL(rwTexture3D->createView({}, rwTexture3DView.writeRef()));
        REQUIRE_CALL(rwTexture3DView->getDescriptorHandle(DescriptorHandleAccess::ReadWrite, &rwTexture3DHandle));
        CHECK(rwTexture3DHandle.type == DescriptorHandleType::RWTexture);
    }

    ComPtr<ITexture> rwTexture1DArray;
    ComPtr<ITextureView> rwTexture1DArrayView;
    DescriptorHandle rwTexture1DArrayHandle = {};
    {
        TextureDesc desc = {};
        desc.type = TextureType::Texture1DArray;
        desc.size = {2, 1, 1};
        desc.arrayLength = 2;
        desc.format = Format::R32Float;
        desc.usage = TextureUsage::UnorderedAccess | TextureUsage::CopySource;
        ;
        float data[2][2] = {{1.f, 2.f}, {3.f, 4.f}};
        SubresourceData subresourceData[] = {{data[0], 8, 0}, {data[1], 8, 0}};
        REQUIRE_CALL(device->createTexture(desc, subresourceData, rwTexture1DArray.writeRef()));
        REQUIRE_CALL(rwTexture1DArray->createView({}, rwTexture1DArrayView.writeRef()));
        REQUIRE_CALL(
            rwTexture1DArrayView->getDescriptorHandle(DescriptorHandleAccess::ReadWrite, &rwTexture1DArrayHandle)
        );
        CHECK(rwTexture1DArrayHandle.type == DescriptorHandleType::RWTexture);
    }

    ComPtr<ITexture> rwTexture2DArray;
    ComPtr<ITextureView> rwTexture2DArrayView;
    DescriptorHandle rwTexture2DArrayHandle = {};
    {
        TextureDesc desc = {};
        desc.type = TextureType::Texture2DArray;
        desc.size = {2, 2, 1};
        desc.arrayLength = 2;
        desc.format = Format::R32Float;
        desc.usage = TextureUsage::UnorderedAccess | TextureUsage::CopySource;
        ;
        float data[2][4] = {{1.f, 2.f, 3.f, 4.f}, {5.f, 6.f, 7.f, 8.f}};
        SubresourceData subresourceData[] = {{data[0], 8, 0}, {data[1], 8, 0}};
        REQUIRE_CALL(device->createTexture(desc, subresourceData, rwTexture2DArray.writeRef()));
        REQUIRE_CALL(rwTexture2DArray->createView({}, rwTexture2DArrayView.writeRef()));
        REQUIRE_CALL(
            rwTexture2DArrayView->getDescriptorHandle(DescriptorHandleAccess::ReadWrite, &rwTexture2DArrayHandle)
        );
        CHECK(rwTexture2DArrayHandle.type == DescriptorHandleType::RWTexture);
    }

    ComPtr<ISampler> samplerPoint;
    DescriptorHandle samplerPointHandle = {};
    if (device->getDeviceType() != DeviceType::CUDA)
    {
        SamplerDesc desc = {};
        desc.minFilter = desc.magFilter = desc.mipFilter = TextureFilteringMode::Point;
        REQUIRE_CALL(device->createSampler(desc, samplerPoint.writeRef()));
        REQUIRE_CALL(samplerPoint->getDescriptorHandle(&samplerPointHandle));
        CHECK(samplerPointHandle.type == DescriptorHandleType::Sampler);
    }

    ComPtr<ISampler> samplerLinear;
    DescriptorHandle samplerLinearHandle = {};
    if (device->getDeviceType() != DeviceType::CUDA)
    {
        SamplerDesc desc = {};
        desc.minFilter = desc.magFilter = desc.mipFilter = TextureFilteringMode::Linear;
        REQUIRE_CALL(device->createSampler(desc, samplerLinear.writeRef()));
        REQUIRE_CALL(samplerLinear->getDescriptorHandle(&samplerLinearHandle));
        CHECK(samplerLinearHandle.type == DescriptorHandleType::Sampler);
    }

    ComPtr<IBuffer> result;
    {
        BufferDesc desc = {};
        desc.size = 1024;
        desc.usage = BufferUsage::UnorderedAccess | BufferUsage::CopySource;
        REQUIRE_CALL(device->createBuffer(desc, nullptr, result.writeRef()));
    }

    {
        auto queue = device->getQueue(QueueType::Graphics);
        auto commandEncoder = queue->createCommandEncoder();
        auto passEncoder = commandEncoder->beginComputePass();
        IShaderObject* rootObject = passEncoder->bindPipeline(pipeline);
        ShaderCursor cursor(rootObject);
        cursor["texture1D"].setDescriptorHandle(texture1DHandle);
        cursor["texture2D"].setDescriptorHandle(texture2DHandle);
        cursor["texture3D"].setDescriptorHandle(texture3DHandle);
        cursor["textureCube"].setDescriptorHandle(textureCubeHandle);
        cursor["texture1DArray"].setDescriptorHandle(texture1DArrayHandle);
        cursor["texture2DArray"].setDescriptorHandle(texture2DArrayHandle);
        cursor["textureCubeArray"].setDescriptorHandle(textureCubeArrayHandle);
        cursor["rwTexture1D"].setDescriptorHandle(rwTexture1DHandle);
        cursor["rwTexture2D"].setDescriptorHandle(rwTexture2DHandle);
        cursor["rwTexture3D"].setDescriptorHandle(rwTexture3DHandle);
        cursor["rwTexture1DArray"].setDescriptorHandle(rwTexture1DArrayHandle);
        cursor["rwTexture2DArray"].setDescriptorHandle(rwTexture2DArrayHandle);
        cursor["samplerPoint"].setDescriptorHandle(samplerPointHandle);
        cursor["samplerLinear"].setDescriptorHandle(samplerLinearHandle);
        cursor["result"].setBinding(result);

        passEncoder->dispatchCompute(1, 1, 1);
        passEncoder->end();

        queue->submit(commandEncoder->finish());
        queue->waitOnHost();
    }

    compareComputeResult(
        device,
        result,
        std::array{
            // Texture1D
            1.f,
            2.f,
            // Texture2D
            1.f,
            4.f,
            // Texture3D
            1.f,
            8.f,
            // TextureCube
            1.f,
            2.f,
            3.f,
            4.f,
            5.f,
            6.f,
            // Texture1DArray
            1.f,
            2.f,
            3.f,
            4.f,
            // Texture2DArray
            1.f,
            4.f,
            5.f,
            8.f,
            // TextureCubeArray
            1.f,
            2.f,
            3.f,
            4.f,
            5.f,
            6.f,
            7.f,
            8.f,
            9.f,
            10.f,
            11.f,
            12.f,
            // RWTexture1D
            1.f,
            2.f,
            // RWTexture2D
            1.f,
            4.f,
            // RWTexture3D
            1.f,
            8.f,
            // RWTexture1DArray
            1.f,
            2.f,
            3.f,
            4.f,
            // RWTexture2DArray
            1.f,
            4.f,
            5.f,
            8.f,
        }
    );

    compareComputeResult(device, rwTexture1D, 0, 0, std::array{2.f, 3.f});
    compareComputeResult(device, rwTexture2D, 0, 0, std::array{2.f, 2.f, 3.f, 5.f});
    compareComputeResult(device, rwTexture3D, 0, 0, std::array{2.f, 2.f, 3.f, 4.f, 5.f, 6.f, 7.f, 9.f});

    compareComputeResult(device, rwTexture1DArray, 0, 0, std::array{2.f, 3.f});
    compareComputeResult(device, rwTexture1DArray, 1, 0, std::array{4.f, 5.f});
    compareComputeResult(device, rwTexture2DArray, 0, 0, std::array{2.f, 2.f, 3.f, 5.f});
    compareComputeResult(device, rwTexture2DArray, 1, 0, std::array{6.f, 6.f, 7.f, 9.f});
}
