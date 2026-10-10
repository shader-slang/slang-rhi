#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

// Writes a write-only storage texture with a declared format and updates a read-write storage
// texture whose format is inferred from its element type. The shader also uses uniform data.
GPU_TEST_CASE("storage-texture-write", D3D12 | Vulkan | Metal | WGPU)
{
    ComPtr<IShaderProgram> shaderProgram;
    REQUIRE_CALL(loadProgram(device, "test-storage-texture", "computeMain", shaderProgram.writeRef()));

    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

    const uint32_t size = 4;

    TextureDesc textureDesc = {};
    textureDesc.type = TextureType::Texture2D;
    textureDesc.size = {size, size, 1};
    textureDesc.mipCount = 1;
    textureDesc.format = Format::RGBA16Float;
    textureDesc.usage = TextureUsage::UnorderedAccess | TextureUsage::CopySource;
    ComPtr<ITexture> writeOnly;
    REQUIRE_CALL(device->createTexture(textureDesc, nullptr, writeOnly.writeRef()));

    float initialData[size * size];
    for (uint32_t i = 0; i < size * size; ++i)
        initialData[i] = float(i);
    SubresourceData subresourceData = {initialData, size * sizeof(float), 0};
    textureDesc.format = Format::R32Float;
    textureDesc.usage = TextureUsage::UnorderedAccess | TextureUsage::CopySource | TextureUsage::CopyDestination;
    ComPtr<ITexture> readWrite;
    REQUIRE_CALL(device->createTexture(textureDesc, &subresourceData, readWrite.writeRef()));

    {
        auto queue = device->getQueue(QueueType::Graphics);
        auto commandEncoder = queue->createCommandEncoder();
        auto passEncoder = commandEncoder->beginComputePass();
        ShaderCursor cursor(passEncoder->bindPipeline(pipeline));
        cursor["value"].setData(0.25f);
        cursor["writeOnly"].setBinding(writeOnly);
        cursor["readWrite"].setBinding(readWrite);
        passEncoder->dispatchCompute(1, 1, 1);
        passEncoder->end();
        queue->submit(commandEncoder->finish());
        queue->waitOnHost();
    }

    // Half-precision bit patterns of (0.25, 0.5, 0.5, 1.0).
    const uint16_t expectedTexel[4] = {0x3400, 0x3800, 0x3800, 0x3C00};
    ComPtr<ISlangBlob> blob;
    SubresourceLayout layout;
    REQUIRE_CALL(device->readTexture(writeOnly, 0, 0, blob.writeRef(), &layout));
    for (uint32_t y = 0; y < size; ++y)
    {
        const uint8_t* row = static_cast<const uint8_t*>(blob->getBufferPointer()) + y * layout.rowPitch;
        for (uint32_t x = 0; x < size; ++x)
        {
            const uint16_t* texel = reinterpret_cast<const uint16_t*>(row) + x * 4;
            for (uint32_t c = 0; c < 4; ++c)
                CHECK_EQ(texel[c], expectedTexel[c]);
        }
    }

    REQUIRE_CALL(device->readTexture(readWrite, 0, 0, blob.writeRef(), &layout));
    for (uint32_t y = 0; y < size; ++y)
    {
        const uint8_t* row = static_cast<const uint8_t*>(blob->getBufferPointer()) + y * layout.rowPitch;
        for (uint32_t x = 0; x < size; ++x)
            CHECK_EQ(reinterpret_cast<const float*>(row)[x], float(y * size + x) + 0.25f);
    }
}
