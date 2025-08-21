#pragma once

#include "cuda-base.h"
#include "../graphics-heap.h"

#include <mutex>

namespace rhi::cuda {

class HeapImpl : public Heap
{
public:
    struct PendingFree
    {
        GraphicsAllocation allocation;
        uint64_t submitIndex;
    };

    class PageImpl : public Heap::Page
    {
    public:
        PageImpl(Heap* heap, const PageDesc& desc, CUdeviceptr cudaMemory)
            : Heap::Page(heap, desc)
            , m_cudaMemory(cudaMemory)
        {
        }

        DeviceAddress offsetToAddress(Size offset) override { return DeviceAddress(m_cudaMemory + offset); }

        CUdeviceptr m_cudaMemory;
    };

    HeapImpl(Device* device, const GraphicsHeapDesc& desc);
    ~HeapImpl();

    virtual SLANG_NO_THROW Result SLANG_MCALL free(GraphicsAllocation allocation) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL flush() override;

    virtual Result allocatePage(const PageDesc& desc, Page** outPage) override;
    virtual Result freePage(Page* page) override;

    std::list<PendingFree> m_pendingFrees;
};

} // namespace rhi::cuda
