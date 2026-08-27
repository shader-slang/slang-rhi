#pragma once

#include "vk-base.h"
#include "../staging-heap.h"

#include <vector>

namespace rhi::vk {

class ConstantBufferPool
{
public:
    struct Allocation
    {
        BufferImpl* buffer;
        size_t offset;
        void* mappedData;
    };

    void init(StagingHeap* heap);
    void finish();
    void reset();

    Result allocate(size_t size, Allocation& outAllocation);

private:
    StagingHeap* m_heap = nullptr;
    std::vector<RefPtr<StagingHeap::Handle>> m_allocations;
};

} // namespace rhi::vk
