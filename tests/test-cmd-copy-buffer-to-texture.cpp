#include "testing.h"

#include "texture-test.h"
#include "resource-desc-utils.h"

using namespace rhi;
using namespace rhi::testing;


Result getSizeAndMakeBuffer(
    TextureTestContext* c,
    const TextureData& bufferData,
    uint64_t* outSize,
    IBuffer** outBuffer
)
{
    // Calculate size to contain the buffer data
    uint64_t totalSize = 0;
    for (auto& subresource : bufferData.subresources)
    {
        totalSize += subresource.layout.sizeInBytes;
    }
    *outSize = totalSize;

    // Create a buffer large enough to hold the entire texture
    BufferDesc bufferDesc;
    bufferDesc.size = totalSize;
    bufferDesc.usage = BufferUsage::CopySource;
    bufferDesc.memoryType = MemoryType::Upload;
    return c->getDevice()->createBuffer(bufferDesc, nullptr, outBuffer);
}

GPU_TEST_CASE("cmd-copy-buffer-to-texture-full", D3D12 | Vulkan | Metal | WGPU | CUDA)
{
    TextureTestOptions options(device);
    options.addVariants(
        TTShape::All,            // all shapes
        TTArray::Both,           // array and non-array
        TTMip::Both,             // with/without mips
        TTMS::Off,               // without multisampling
        TTPowerOf2::Both,        // test both power-of-2 and non-power-of-2 sizes where possible
        TTFmtStencil::Off,       // no stencil formats
        TextureInitMode::Invalid // don't init texture at all
    );

    runTextureTest(
        options,
        [](TextureTestContext* c)
        {
            auto device = c->getDevice();

            // Get cpu side data (will not be initialized)
            TextureData& data = c->getTextureData();
            // fprintf(stderr, "Uploading texture %s\n", c->getTexture()->getDesc().label);

            // Create some new cpu side data we're going to use for the buffer,
            // initialized with random data and a 256B row alignment.
            TextureData textureData;
            textureData.init(device, data.desc, TextureInitMode::Random, 123, 256);

            // Create a buffer.
            uint64_t totalSize;
            ComPtr<IBuffer> buffer;
            REQUIRE_CALL(getSizeAndMakeBuffer(c, textureData, &totalSize, buffer.writeRef()));

            // Map buffer for write, and copy subresources in
            void* bufferData_ = nullptr;
            REQUIRE_CALL(device->mapBuffer(buffer, CpuAccessMode::Write, &bufferData_));
            uint8_t* bufferData = (uint8_t*)bufferData_;
            for (uint32_t layer = 0; layer < textureData.desc.getLayerCount(); layer++)
            {
                for (uint32_t mip = 0; mip < textureData.desc.mipCount; mip++)
                {
                    const TextureData::Subresource& subresource = textureData.getSubresource(layer, mip);
                    memcpy(bufferData, subresource.data.get(), subresource.layout.sizeInBytes);
                    bufferData += subresource.layout.sizeInBytes;
                }
            }
            REQUIRE_CALL(device->unmapBuffer(buffer));

            // Create command encoder
            auto queue = device->getQueue(QueueType::Graphics);
            auto commandEncoder = queue->createCommandEncoder();

            // Copy entire buffer to texture
            uint64_t bufferOffset = 0;
            for (uint32_t layer = 0; layer < data.desc.getLayerCount(); layer++)
            {
                for (uint32_t mip = 0; mip < data.desc.mipCount; mip++)
                {
                    const TextureData::Subresource& subresource = textureData.getSubresource(layer, mip);

                    commandEncoder->copyBufferToTexture(
                        c->getTexture(),
                        layer,
                        mip,
                        {0, 0, 0},
                        buffer,
                        bufferOffset,
                        subresource.layout.sizeInBytes,
                        subresource.layout.rowPitch,
                        subresource.layout.size
                    );

                    bufferOffset += subresource.layout.sizeInBytes;
                }
            }
            queue->submit(commandEncoder->finish());

            queue->waitOnHost();

            // Check the texture data we copied to the buffer now matches
            // the texture
            textureData.checkEqual(c->getTexture());
        }
    );
}
