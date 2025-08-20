#include "testing.h"

#include <string>
#include <map>
#include <functional>
#include <memory>
#include <random>
#include <thread>

#include "rhi-shared.h"

using namespace rhi;
using namespace rhi::testing;

GPU_TEST_CASE("graphics-heap-create", CUDA)
{
    GraphicsHeapDesc desc;
    desc.label = "Test Graphics Heap";
    desc.memoryType = MemoryType::DeviceLocal;

    ComPtr<IGraphicsHeap> heap;
    device->createGraphicsHeap(desc, heap.writeRef());
}

GPU_TEST_CASE("graphics-heap-allocate", CUDA)
{
    GraphicsHeapDesc desc;
    desc.label = "Test Graphics Heap";
    desc.memoryType = MemoryType::DeviceLocal;

    ComPtr<IGraphicsHeap> heap;
    device->createGraphicsHeap(desc, heap.writeRef());

    GraphicsAllocDesc allocDesc;
    allocDesc.size = 1024 * 1024;     // 1 MB
    allocDesc.alignment = 256 * 1024; // 256 KB

    GraphicsAllocation allocation;
    Result res = heap->allocate(allocDesc, &allocation);
    CHECK_EQ(res, SLANG_OK);
    CHECK_EQ(allocation.size, allocDesc.size);
}
