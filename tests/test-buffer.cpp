#include "testing.h"

#include "rhi-shared.h"

#include <cstring>
#include <vector>

using namespace rhi;
using namespace rhi::testing;

GPU_TEST_CASE("buffer-init-data-staging-lifetime", Vulkan)
{
    auto queue = device->getQueue(QueueType::Graphics);
    REQUIRE_CALL(queue->waitOnHost());

    StagingHeap& heap = getUnderlyingDevice(device)->m_uploadHeap;
    CHECK_EQ(heap.getUsed(), 0);

    std::vector<uint32_t> data(1024);
    for (size_t i = 0; i < data.size(); ++i)
        data[i] = uint32_t(i * 1664525u + 1013904223u);

    BufferDesc desc = {};
    desc.size = data.size() * sizeof(uint32_t);
    desc.memoryType = MemoryType::DeviceLocal;
    desc.usage = BufferUsage::CopySource;

    ComPtr<IBuffer> buffer;
    REQUIRE_CALL(device->createBuffer(desc, data.data(), buffer.writeRef()));

    // Waiting on the public queue also retires work submitted through Vulkan's
    // internal queue wrapper, which must release the shared staging allocation.
    REQUIRE_CALL(queue->waitOnHost());
    CHECK_EQ(heap.getUsed(), 0);

    ComPtr<ISlangBlob> blob;
    REQUIRE_CALL(device->readBuffer(buffer, 0, desc.size, blob.writeRef()));
    CHECK_EQ(memcmp(blob->getBufferPointer(), data.data(), desc.size), 0);

    // Releasing the destination immediately must not destroy its native buffer
    // while the internal initialization copy is still in flight.
    buffer.setNull();
    REQUIRE_CALL(device->createBuffer(desc, data.data(), buffer.writeRef()));
    buffer.setNull();
    REQUIRE_CALL(queue->waitOnHost());
    CHECK_EQ(heap.getUsed(), 0);
}

GPU_TEST_CASE("buffer-release-while-mapped", D3D12 | Vulkan)
{
    BufferDesc desc = {};
    desc.size = 256;
    desc.memoryType = MemoryType::Upload;
    desc.usage = BufferUsage::CopySource;

    // Releasing a buffer that is still mapped implicitly unmaps it.
    for (int mapCount = 1; mapCount <= 2; ++mapCount)
    {
        ComPtr<IBuffer> buffer;
        REQUIRE_CALL(device->createBuffer(desc, nullptr, buffer.writeRef()));
        for (int i = 0; i < mapCount; ++i)
        {
            void* data = nullptr;
            REQUIRE_CALL(device->mapBuffer(buffer, CpuAccessMode::Write, &data));
            REQUIRE(data != nullptr);
            memset(data, 0xab, desc.size);
        }
        buffer.setNull();
        REQUIRE_CALL(device->getQueue(QueueType::Graphics)->waitOnHost());
    }
}

GPU_TEST_CASE("shared-handle-requires-shared-usage", Vulkan)
{
    // Resources created without the Shared usage do not own exportable memory.
    BufferDesc bufferDesc = {};
    bufferDesc.size = 256;
    bufferDesc.usage = BufferUsage::CopySource;
    ComPtr<IBuffer> buffer;
    REQUIRE_CALL(device->createBuffer(bufferDesc, nullptr, buffer.writeRef()));
    NativeHandle handle = {};
    CHECK_EQ(buffer->getSharedHandle(&handle), SLANG_E_INVALID_ARG);

    TextureDesc textureDesc = {};
    textureDesc.type = TextureType::Texture2D;
    textureDesc.format = Format::RGBA8Unorm;
    textureDesc.size = {16, 16, 1};
    textureDesc.usage = TextureUsage::CopySource;
    ComPtr<ITexture> texture;
    REQUIRE_CALL(device->createTexture(textureDesc, nullptr, texture.writeRef()));
    CHECK_EQ(texture->getSharedHandle(&handle), SLANG_E_INVALID_ARG);
}
