#pragma once

#include "cuda-base.h"
#include "../graphics-heap.h"

#include <mutex>

namespace rhi::cuda {

class GraphicsHeapImpl : public GraphicsHeap
{
public:
    struct PendingFree
    {
        GraphicsAllocation allocation;
        uint64_t submitIndex;
    };

    class PageImpl : public GraphicsHeap::Page
    {
    public:
        PageImpl(GraphicsHeap* heap, const PageDesc& desc, CUdeviceptr cudaMemory)
            : GraphicsHeap::Page(heap, desc)
            , m_cudaMemory(cudaMemory)
        {
        }

        DeviceAddress offsetToAddress(Size offset) override { return DeviceAddress(m_cudaMemory + offset); }

        CUdeviceptr m_cudaMemory;
    };

    GraphicsHeapImpl(Device* device, const GraphicsHeapDesc& desc);
    ~GraphicsHeapImpl();

    virtual SLANG_NO_THROW Result SLANG_MCALL free(GraphicsAllocation allocation) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL flush() override;

    virtual Result allocatePage(const PageDesc& desc, Page** page) override;
    virtual Result freePage(Page* page) override;

    Result checkPendingFrees();

    std::list<PendingFree> m_pendingFrees;
};

} // namespace rhi::cuda
