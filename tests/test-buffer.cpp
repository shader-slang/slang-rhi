#include "testing.h"

#include "debug-layer/debug-device.h"
#include "rhi-shared.h"

#include <cstring>
#include <vector>

using namespace rhi;
using namespace rhi::testing;

namespace {

Device* getSharedDevice(IDevice* device)
{
    if (auto debugDevice = dynamic_cast<debug::DebugDevice*>(device))
        return (Device*)debugDevice->baseObject.get();
    return (Device*)device;
}

} // namespace

GPU_TEST_CASE("buffer-init-data-staging-lifetime", Vulkan)
{
    auto queue = device->getQueue(QueueType::Graphics);
    REQUIRE_CALL(queue->waitOnHost());

    StagingHeap& heap = getSharedDevice(device)->m_uploadHeap;
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
