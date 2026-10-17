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

GPU_TEST_CASE("buffer-readback-staging-pool", Vulkan | DontCacheDevice)
{
    // A fresh (uncached) device starts with an empty readback heap, so the page-count
    // transitions below prove readBuffer allocates from and reuses the pooled heap.
    auto queue = device->getQueue(QueueType::Graphics);
    REQUIRE_CALL(queue->waitOnHost());

    StagingHeap& readbackHeap = getUnderlyingDevice(device)->m_readbackHeap;
    CHECK_EQ(readbackHeap.getNumPages(), 0);
    CHECK_EQ(readbackHeap.getCapacity(), 0);

    std::vector<uint32_t> data(1024);
    for (size_t i = 0; i < data.size(); ++i)
        data[i] = uint32_t(i * 2654435761u + 2166136261u);

    BufferDesc desc = {};
    desc.size = data.size() * sizeof(uint32_t);
    desc.memoryType = MemoryType::DeviceLocal;
    desc.usage = BufferUsage::CopySource;

    ComPtr<IBuffer> buffer;
    REQUIRE_CALL(device->createBuffer(desc, data.data(), buffer.writeRef()));

    {
        std::vector<uint8_t> readback(desc.size);
        REQUIRE_CALL(device->readBuffer(buffer, 0, desc.size, readback.data()));
        CHECK_EQ(memcmp(readback.data(), data.data(), desc.size), 0);
    }

    CHECK_EQ(readbackHeap.getNumPages(), 1);
    const Size retainedCapacity = readbackHeap.getCapacity();
    CHECK(retainedCapacity > 0);

    const size_t elemOffset = 4;
    const Size byteOffset = elemOffset * sizeof(uint32_t);
    const Size byteSize = 16 * sizeof(uint32_t);
    for (int iter = 0; iter < 8; ++iter)
    {
        std::vector<uint32_t> readback(16);
        REQUIRE_CALL(device->readBuffer(buffer, byteOffset, byteSize, readback.data()));
        CHECK_EQ(memcmp(readback.data(), data.data() + elemOffset, byteSize), 0);
    }

    CHECK_EQ(readbackHeap.getNumPages(), 1);
    CHECK_EQ(readbackHeap.getCapacity(), retainedCapacity);

    CHECK_EQ(readbackHeap.getUsed(), 0);
    REQUIRE_CALL(queue->waitOnHost());
    CHECK_EQ(readbackHeap.getUsed(), 0);
}

GPU_TEST_CASE("buffer-readback-arg-validation", Vulkan)
{
    // Call the backend device directly (bypassing the debug layer) so the checks exercised
    // are readBuffer's own argument validation.
    Device* backend = getUnderlyingDevice(device);

    BufferDesc desc = {};
    desc.size = 256;
    desc.memoryType = MemoryType::DeviceLocal;
    desc.usage = BufferUsage::CopySource;

    std::vector<uint8_t> src(desc.size);
    for (size_t i = 0; i < src.size(); ++i)
        src[i] = uint8_t(i);

    ComPtr<IBuffer> buffer;
    REQUIRE_CALL(backend->createBuffer(desc, src.data(), buffer.writeRef()));

    std::vector<uint8_t> dst(desc.size);

    CHECK_EQ(backend->readBuffer(buffer, 0, desc.size, dst.data()), SLANG_OK);
    CHECK_EQ(memcmp(dst.data(), src.data(), desc.size), 0);

    CHECK_EQ(backend->readBuffer(buffer, 0, desc.size + 1, dst.data()), SLANG_E_INVALID_ARG);
    CHECK_EQ(backend->readBuffer(buffer, desc.size, 1, dst.data()), SLANG_E_INVALID_ARG);

    // Huge offset (offset > size) rejected by the first clause; offset + size also wraps here.
    CHECK_EQ(backend->readBuffer(buffer, ~Size(0) - 3, 16, dst.data()), SLANG_E_INVALID_ARG);
    // In-range offset with a size that overflows offset + size: exercises the
    // `size > desc.size - offset` branch that replaced the old wrapping check.
    CHECK_EQ(backend->readBuffer(buffer, 4, ~Size(0), dst.data()), SLANG_E_INVALID_ARG);

    CHECK_EQ(backend->readBuffer((IBuffer*)nullptr, 0, 4, dst.data()), SLANG_E_INVALID_ARG);
    CHECK_EQ(backend->readBuffer(buffer, 0, 4, (void*)nullptr), SLANG_E_INVALID_ARG);
    CHECK_EQ(backend->readBuffer(buffer, 0, 0, dst.data()), SLANG_E_INVALID_ARG);
}
