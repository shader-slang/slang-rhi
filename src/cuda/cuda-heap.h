#pragma once

#include "cuda-base.h"
#include "../heap.h"

#include <mutex>

namespace rhi::cuda {

class HeapImpl : public Heap
{
public:
    // Selected based on (https://docs.nvidia.com/cuda/cuda-c-programming-guide/index.html#alignment)
    // Highest alignment requirement in CUDA is 128, for optimal memcpy performance.
    static const Size kAlignment = 128;

    struct PendingFree
    {
        HeapAlloc allocation;
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

    HeapImpl(Device* device, const HeapDesc& desc);
    ~HeapImpl();

    virtual SLANG_NO_THROW Result SLANG_MCALL free(HeapAlloc allocation) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL flush() override;

    virtual Result allocatePage(const PageDesc& desc, Page** outPage) override;
    virtual Result freePage(Page* page) override;

    // Alignments
    virtual Result fixUpAllocDesc(HeapAllocDesc& desc) override;

    std::list<PendingFree> m_pendingFrees;
};

} // namespace rhi::cuda
