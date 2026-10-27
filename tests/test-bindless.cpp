#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

// Bindless buffers are currently not supported on CUDA.
GPU_TEST_CASE("bindless-buffers", D3D12 | Vulkan | Metal)
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
        REQUIRE_CALL(cursor["params"]["buffer"].setDescriptorHandle(bufferHandle));
        REQUIRE_CALL(cursor["params"]["structuredBuffer"].setDescriptorHandle(structuredBufferHandle));
        REQUIRE_CALL(cursor["params"]["byteAddressBuffer"].setDescriptorHandle(byteAddressBufferHandle));
        REQUIRE_CALL(cursor["params"]["rwBuffer"].setDescriptorHandle(rwBufferHandle));
        REQUIRE_CALL(cursor["params"]["rwStructuredBuffer"].setDescriptorHandle(rwStructuredBufferHandle));
        REQUIRE_CALL(cursor["params"]["rwByteAddressBuffer"].setDescriptorHandle(rwByteAddressBufferHandle));
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

GPU_TEST_CASE("bindless-textures", D3D12 | Vulkan | CUDA | Metal)
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
        REQUIRE_CALL(cursor["params"]["texture1D"].setDescriptorHandle(texture1DHandle));
        REQUIRE_CALL(cursor["params"]["texture2D"].setDescriptorHandle(texture2DHandle));
        REQUIRE_CALL(cursor["params"]["texture3D"].setDescriptorHandle(texture3DHandle));
        REQUIRE_CALL(cursor["params"]["textureCube"].setDescriptorHandle(textureCubeHandle));
        REQUIRE_CALL(cursor["params"]["texture1DArray"].setDescriptorHandle(texture1DArrayHandle));
        REQUIRE_CALL(cursor["params"]["texture2DArray"].setDescriptorHandle(texture2DArrayHandle));
        REQUIRE_CALL(cursor["params"]["textureCubeArray"].setDescriptorHandle(textureCubeArrayHandle));
        REQUIRE_CALL(cursor["params"]["rwTexture1D"].setDescriptorHandle(rwTexture1DHandle));
        REQUIRE_CALL(cursor["params"]["rwTexture2D"].setDescriptorHandle(rwTexture2DHandle));
        REQUIRE_CALL(cursor["params"]["rwTexture3D"].setDescriptorHandle(rwTexture3DHandle));
        REQUIRE_CALL(cursor["params"]["rwTexture1DArray"].setDescriptorHandle(rwTexture1DArrayHandle));
        REQUIRE_CALL(cursor["params"]["rwTexture2DArray"].setDescriptorHandle(rwTexture2DArrayHandle));
        REQUIRE_CALL(cursor["params"]["samplerPoint"].setDescriptorHandle(samplerPointHandle));
        REQUIRE_CALL(cursor["params"]["samplerLinear"].setDescriptorHandle(samplerLinearHandle));
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

GPU_TEST_CASE("bindless-combined-texture-samplers", D3D12 | Vulkan | CUDA)
{
    if (!device->hasFeature(Feature::Bindless))
    {
        SKIP("Bindless is not supported");
    }

    ComPtr<IShaderProgram> shaderProgram;
    REQUIRE_CALL(
        loadProgram(device, "test-bindless-combined-texture-samplers", "computeMain", shaderProgram.writeRef())
    );

    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

    ComPtr<ISampler> samplerPoint;
    {
        SamplerDesc desc = {};
        desc.minFilter = desc.magFilter = desc.mipFilter = TextureFilteringMode::Point;
        REQUIRE_CALL(device->createSampler(desc, samplerPoint.writeRef()));
    }

    ComPtr<ISampler> samplerLinear;
    {
        SamplerDesc desc = {};
        desc.minFilter = desc.magFilter = desc.mipFilter = TextureFilteringMode::Linear;
        REQUIRE_CALL(device->createSampler(desc, samplerLinear.writeRef()));
    }

    ComPtr<ITexture> texture2D;
    ComPtr<ITextureView> texture2DViewPoint;
    ComPtr<ITextureView> texture2DViewLinear;
    DescriptorHandle combinedSampler2DPointHandle = {};
    DescriptorHandle combinedSampler2DLinearHandle = {};
    {
        TextureDesc desc = {};
        desc.type = TextureType::Texture2D;
        desc.size = {2, 2, 1};
        desc.format = Format::R32Float;
        desc.usage = TextureUsage::ShaderResource;
        float data[4] = {1.f, 2.f, 3.f, 4.f};
        SubresourceData subresourceData[] = {{data, 8, 0}};
        REQUIRE_CALL(device->createTexture(desc, subresourceData, texture2D.writeRef()));

        TextureViewDesc viewDescPoint = {};
        viewDescPoint.sampler = samplerPoint;
        REQUIRE_CALL(texture2D->createView(viewDescPoint, texture2DViewPoint.writeRef()));
        REQUIRE_CALL(texture2DViewPoint->getCombinedTextureSamplerDescriptorHandle(&combinedSampler2DPointHandle));
        CHECK(combinedSampler2DPointHandle.type == DescriptorHandleType::CombinedTextureSampler);

        TextureViewDesc viewDescLinear = {};
        viewDescLinear.sampler = samplerLinear;
        REQUIRE_CALL(texture2D->createView(viewDescLinear, texture2DViewLinear.writeRef()));
        REQUIRE_CALL(texture2DViewLinear->getCombinedTextureSamplerDescriptorHandle(&combinedSampler2DLinearHandle));
        CHECK(combinedSampler2DLinearHandle.type == DescriptorHandleType::CombinedTextureSampler);
    }

    ComPtr<ITexture> texture3D;
    ComPtr<ITextureView> texture3DView;
    DescriptorHandle combinedSampler3DHandle = {};
    {
        TextureDesc desc = {};
        desc.type = TextureType::Texture3D;
        desc.size = {2, 2, 2};
        desc.format = Format::R32Float;
        desc.usage = TextureUsage::ShaderResource;
        float data[8] = {1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 7.f, 8.f};
        SubresourceData subresourceData[] = {{data, 8, 16}};
        REQUIRE_CALL(device->createTexture(desc, subresourceData, texture3D.writeRef()));
        TextureViewDesc viewDesc = {};
        viewDesc.sampler = samplerPoint;
        REQUIRE_CALL(texture3D->createView(viewDesc, texture3DView.writeRef()));
        REQUIRE_CALL(texture3DView->getCombinedTextureSamplerDescriptorHandle(&combinedSampler3DHandle));
        CHECK(combinedSampler3DHandle.type == DescriptorHandleType::CombinedTextureSampler);
    }

    ComPtr<ITexture> textureCube;
    ComPtr<ITextureView> textureCubeView;
    DescriptorHandle combinedSamplerCubeHandle = {};
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
        TextureViewDesc viewDesc = {};
        viewDesc.sampler = samplerPoint;
        REQUIRE_CALL(textureCube->createView(viewDesc, textureCubeView.writeRef()));
        REQUIRE_CALL(textureCubeView->getCombinedTextureSamplerDescriptorHandle(&combinedSamplerCubeHandle));
        CHECK(combinedSamplerCubeHandle.type == DescriptorHandleType::CombinedTextureSampler);
    }

    ComPtr<ITexture> texture2DArray;
    ComPtr<ITextureView> texture2DArrayView;
    DescriptorHandle combinedSampler2DArrayHandle = {};
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
        TextureViewDesc viewDesc = {};
        viewDesc.sampler = samplerPoint;
        REQUIRE_CALL(texture2DArray->createView(viewDesc, texture2DArrayView.writeRef()));
        REQUIRE_CALL(texture2DArrayView->getCombinedTextureSamplerDescriptorHandle(&combinedSampler2DArrayHandle));
        CHECK(combinedSampler2DArrayHandle.type == DescriptorHandleType::CombinedTextureSampler);
    }

    ComPtr<ITexture> textureCubeArray;
    ComPtr<ITextureView> textureCubeArrayView;
    DescriptorHandle combinedSamplerCubeArrayHandle = {};
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
        TextureViewDesc viewDesc = {};
        viewDesc.sampler = samplerPoint;
        REQUIRE_CALL(textureCubeArray->createView(viewDesc, textureCubeArrayView.writeRef()));
        REQUIRE_CALL(textureCubeArrayView->getCombinedTextureSamplerDescriptorHandle(&combinedSamplerCubeArrayHandle));
        CHECK(combinedSamplerCubeArrayHandle.type == DescriptorHandleType::CombinedTextureSampler);
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
        cursor["combinedSampler2DPoint"].setDescriptorHandle(combinedSampler2DPointHandle);
        cursor["combinedSampler2DLinear"].setDescriptorHandle(combinedSampler2DLinearHandle);
        cursor["combinedSampler3D"].setDescriptorHandle(combinedSampler3DHandle);
        cursor["combinedSamplerCube"].setDescriptorHandle(combinedSamplerCubeHandle);
        cursor["combinedSampler2DArray"].setDescriptorHandle(combinedSampler2DArrayHandle);
        cursor["combinedSamplerCubeArray"].setDescriptorHandle(combinedSamplerCubeArrayHandle);
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
            // Sampler2D (point)
            1.f,
            4.f,
            // Sampler2D (linear) - average of 1, 2, 3, 4
            2.5f,
            // Sampler3D
            1.f,
            8.f,
            // SamplerCube
            1.f,
            2.f,
            3.f,
            4.f,
            5.f,
            6.f,
            // Sampler2DArray
            1.f,
            4.f,
            5.f,
            8.f,
            // SamplerCubeArray
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
        }
    );
}

// Metal handles are native 64-bit values. Keep targets alive explicitly: the only
// shader binding referencing them is a device-local buffer containing these records.
GPU_TEST_CASE("bindless-records", Metal)
{
    if (!device->hasFeature(Feature::Bindless))
        SKIP("Bindless requires a residency set on Metal");

    struct Record
    {
        uint64_t buffer;
        uint64_t typedBuffer;
        uint64_t writableBuffer;
        uint64_t texture;
        uint64_t sampler;
    };
    static_assert(sizeof(Record) == 40);
    Record records[2] = {};

    std::array<float, 132> values = {};
    values[64] = 3.f;
    values[65] = 7.f;
    values[128] = 5.f;
    values[129] = 11.f;
    BufferDesc bufferDesc = {};
    bufferDesc.size = sizeof(values);
    bufferDesc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess | BufferUsage::CopySource;
    ComPtr<IBuffer> buffer;
    REQUIRE_CALL(device->createBuffer(bufferDesc, values.data(), buffer.writeRef()));

    SamplerDesc samplerDesc = {};
    samplerDesc.minFilter = TextureFilteringMode::Point;
    samplerDesc.magFilter = TextureFilteringMode::Point;
    ComPtr<ISampler> sampler;
    REQUIRE_CALL(device->createSampler(samplerDesc, sampler.writeRef()));
    DescriptorHandle samplerHandle;
    REQUIRE_CALL(sampler->getDescriptorHandle(&samplerHandle));

    // Each record selects a different layer, and each view starts at mip 1.
    TextureDesc textureDesc = {};
    textureDesc.type = TextureType::Texture2DArray;
    textureDesc.size = {2, 2, 1};
    textureDesc.arrayLength = 2;
    textureDesc.mipCount = 2;
    textureDesc.format = Format::R32Float;
    textureDesc.usage = TextureUsage::ShaderResource;
    const float baseMip[4] = {-1.f, -1.f, -1.f, -1.f};
    const float mipValues[2] = {13.f, 17.f};
    SubresourceData textureData[] = {
        {baseMip, 8, 16},
        {&mipValues[0], 4, 4},
        {baseMip, 8, 16},
        {&mipValues[1], 4, 4},
    };
    ComPtr<ITexture> texture;
    REQUIRE_CALL(device->createTexture(textureDesc, textureData, texture.writeRef()));
    ComPtr<ITextureView> views[2];
    for (uint32_t i = 0; i < 2; ++i)
    {
        BufferRange range = {256 * (i + 1), 8};
        DescriptorHandle handle;
        REQUIRE_CALL(buffer->getDescriptorHandle(DescriptorHandleAccess::Read, Format::Undefined, range, &handle));
        records[i].buffer = handle.value;
        REQUIRE_CALL(buffer->getDescriptorHandle(DescriptorHandleAccess::Read, Format::R32Float, range, &handle));
        records[i].typedBuffer = handle.value;
        DescriptorHandle repeated;
        REQUIRE_CALL(buffer->getDescriptorHandle(DescriptorHandleAccess::Read, Format::R32Float, range, &repeated));
        CHECK(repeated.value == handle.value);
        REQUIRE_CALL(buffer->getDescriptorHandle(DescriptorHandleAccess::ReadWrite, Format::R32Float, range, &handle));
        records[i].writableBuffer = handle.value;

        TextureViewDesc viewDesc = {};
        viewDesc.subresourceRange = {i, 1, 1, 1};
        REQUIRE_CALL(device->createTextureView(texture, viewDesc, views[i].writeRef()));
        REQUIRE_CALL(views[i]->getDescriptorHandle(DescriptorHandleAccess::Read, &handle));
        records[i].texture = handle.value;
        records[i].sampler = samplerHandle.value;
    }

    BufferDesc recordsDesc = {};
    recordsDesc.size = sizeof(records);
    recordsDesc.elementSize = sizeof(Record);
    recordsDesc.usage = BufferUsage::ShaderResource;
    ComPtr<IBuffer> recordsBuffer;
    REQUIRE_CALL(device->createBuffer(recordsDesc, records, recordsBuffer.writeRef()));

    auto queue = device->getQueue(QueueType::Graphics);
    SUBCASE("compute")
    {
        ComPtr<IShaderProgram> program;
        REQUIRE_CALL(loadProgram(device, "test-bindless-records", "computeMain", program.writeRef()));
        ComputePipelineDesc pipelineDesc = {};
        pipelineDesc.program = program;
        ComPtr<IComputePipeline> pipeline;
        REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

        BufferDesc resultDesc = {};
        resultDesc.size = 2 * sizeof(float);
        resultDesc.usage = BufferUsage::UnorderedAccess | BufferUsage::CopySource;
        ComPtr<IBuffer> result;
        REQUIRE_CALL(device->createBuffer(resultDesc, nullptr, result.writeRef()));
        auto encoder = queue->createCommandEncoder();
        auto pass = encoder->beginComputePass();
        ShaderCursor cursor(pass->bindPipeline(pipeline));
        REQUIRE_CALL(cursor["records"].setBinding(recordsBuffer));
        REQUIRE_CALL(cursor["result"].setBinding(result));
        pass->dispatchCompute(2, 1, 1);
        pass->end();
        REQUIRE_CALL(queue->submit(encoder->finish()));
        REQUIRE_CALL(queue->waitOnHost());
        compareComputeResult(device, result, std::array{23.f, 33.f});
        values[64] += 1;
        values[128] += 1;
        compareComputeResult(device, buffer, values);
    }
    SUBCASE("graphics")
    {
        ComPtr<IShaderProgram> program;
        REQUIRE_CALL(loadProgram(device, "test-bindless-records", {"vertexMain", "fragmentMain"}, program.writeRef()));
        ColorTargetDesc target = {};
        target.format = Format::RGBA32Float;
        RenderPipelineDesc pipelineDesc = {};
        pipelineDesc.program = program;
        pipelineDesc.targets = &target;
        pipelineDesc.targetCount = 1;
        ComPtr<IRenderPipeline> pipeline;
        REQUIRE_CALL(device->createRenderPipeline(pipelineDesc, pipeline.writeRef()));
        TextureDesc outputDesc = {};
        outputDesc.size = {2, 1, 1};
        outputDesc.format = target.format;
        outputDesc.usage = TextureUsage::RenderTarget | TextureUsage::CopySource;
        ComPtr<ITexture> output;
        REQUIRE_CALL(device->createTexture(outputDesc, nullptr, output.writeRef()));

        auto encoder = queue->createCommandEncoder();
        RenderPassColorAttachment attachment = {};
        attachment.view = output->getDefaultView();
        RenderPassDesc passDesc = {};
        passDesc.colorAttachments = &attachment;
        passDesc.colorAttachmentCount = 1;
        auto pass = encoder->beginRenderPass(passDesc);
        ShaderCursor cursor(pass->bindPipeline(pipeline));
        REQUIRE_CALL(cursor["records"].setBinding(recordsBuffer));
        RenderState state = {};
        state.viewports[0] = Viewport::fromSize(2, 1);
        state.viewportCount = 1;
        state.scissorRects[0] = ScissorRect::fromSize(2, 1);
        state.scissorRectCount = 1;
        pass->setRenderState(state);
        DrawArguments args = {};
        args.vertexCount = 3;
        pass->draw(args);
        pass->end();
        REQUIRE_CALL(queue->submit(encoder->finish()));
        REQUIRE_CALL(queue->waitOnHost());
        ComPtr<ISlangBlob> data;
        SubresourceLayout layout;
        REQUIRE_CALL(device->readTexture(output, 0, 0, data.writeRef(), &layout));
        const float* pixels = static_cast<const float*>(data->getBufferPointer());
        CHECK(pixels[0] == 23.f);
        CHECK(pixels[4] == 33.f);
    }
}

GPU_TEST_CASE("bindless-metal-availability", Metal)
{
    bool supported = device->hasFeature(Feature::ResidencySet);
    CHECK(device->hasFeature(Feature::Bindless) == supported);
    BufferDesc bufferDesc = {};
    bufferDesc.size = 256;
    bufferDesc.usage = BufferUsage::ShaderResource;
    ComPtr<IBuffer> buffer;
    REQUIRE_CALL(device->createBuffer(bufferDesc, nullptr, buffer.writeRef()));
    TextureDesc textureDesc = {};
    textureDesc.format = Format::R32Float;
    textureDesc.usage = TextureUsage::ShaderResource;
    ComPtr<ITexture> texture;
    REQUIRE_CALL(device->createTexture(textureDesc, nullptr, texture.writeRef()));
    ComPtr<ISampler> sampler;
    REQUIRE_CALL(device->createSampler({}, sampler.writeRef()));
    Result expected = supported ? SLANG_OK : SLANG_E_NOT_AVAILABLE;
    DescriptorHandle handle = {};
    CHECK(
        buffer->getDescriptorHandle(DescriptorHandleAccess::Read, Format::Undefined, kEntireBuffer, &handle) == expected
    );
    CHECK(bool(handle) == supported);
    CHECK(
        buffer->getDescriptorHandle(DescriptorHandleAccess::Read, Format::R32Float, kEntireBuffer, &handle) == expected
    );
    CHECK(bool(handle) == supported);
    CHECK(texture->getDefaultView()->getDescriptorHandle(DescriptorHandleAccess::Read, &handle) == expected);
    CHECK(bool(handle) == supported);
    CHECK(sampler->getDescriptorHandle(&handle) == expected);
    CHECK(bool(handle) == supported);
    CHECK(texture->getDefaultView()->getCombinedTextureSamplerDescriptorHandle(&handle) == SLANG_E_NOT_AVAILABLE);

    if (supported)
    {
        CHECK(
            buffer->getDescriptorHandle(DescriptorHandleAccess::Read, Format::R32Float, {1, 4}, &handle) ==
            SLANG_E_INVALID_ARG
        );
        CHECK_FALSE(handle);
        CHECK(
            buffer->getDescriptorHandle(DescriptorHandleAccess::Read, Format::R32Float, {0, 3}, &handle) ==
            SLANG_E_INVALID_ARG
        );
        CHECK(
            buffer->getDescriptorHandle(DescriptorHandleAccess::Read, Format::Undefined, {256, 0}, &handle) ==
            SLANG_E_INVALID_ARG
        );
        CHECK(
            buffer->getDescriptorHandle(DescriptorHandleAccess::Read, Format::BC1Unorm, kEntireBuffer, &handle) ==
            SLANG_E_NOT_AVAILABLE
        );
    }
}

// The same texture-buffer view implementation is used for ordinary bindings.
// Exercise both layouts even when bindless is unavailable (the residency fallback).
GPU_TEST_CASE("metal-typed-buffer-bindings", Metal)
{
    bool parameterBlock = false;
    SUBCASE("root") {}
    SUBCASE("parameter-block")
    {
        parameterBlock = true;
    }
    const char* rootSource = R"(
        Buffer<uint> source;
        RWBuffer<uint> destination;
        [shader("compute")] [numthreads(1, 1, 1)]
        void computeMain() { destination[0] = source[0] + 1; destination[1] = source[1] + 2; }
    )";
    const char* blockSource = R"(
        struct Params { Buffer<uint> source; RWBuffer<uint> destination; };
        ParameterBlock<Params> params;
        [shader("compute")] [numthreads(1, 1, 1)]
        void computeMain() { params.destination[0] = params.source[0] + 1; params.destination[1] = params.source[1] + 2; }
    )";
    ComPtr<IShaderProgram> program;
    REQUIRE_CALL(loadComputeProgramFromSource(device, parameterBlock ? blockSource : rootSource, program.writeRef()));
    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = program;
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));
    BufferDesc desc = {};
    desc.size = 8;
    desc.format = Format::R32Uint;
    desc.usage = BufferUsage::ShaderResource;
    const uint32_t input[] = {41, 100};
    ComPtr<IBuffer> source;
    REQUIRE_CALL(device->createBuffer(desc, input, source.writeRef()));
    desc.usage = BufferUsage::UnorderedAccess | BufferUsage::CopySource;
    ComPtr<IBuffer> destination;
    REQUIRE_CALL(device->createBuffer(desc, nullptr, destination.writeRef()));
    auto queue = device->getQueue(QueueType::Graphics);
    auto encoder = queue->createCommandEncoder();
    auto pass = encoder->beginComputePass();
    ShaderCursor cursor(pass->bindPipeline(pipeline));
    if (parameterBlock)
        cursor = cursor["params"];
    REQUIRE_CALL(cursor["source"].setBinding(source));
    REQUIRE_CALL(cursor["destination"].setBinding(destination));
    pass->dispatchCompute(1, 1, 1);
    pass->end();
    REQUIRE_CALL(queue->submit(encoder->finish()));
    REQUIRE_CALL(queue->waitOnHost());
    compareComputeResult(device, destination, std::array{42u, 102u});
}
