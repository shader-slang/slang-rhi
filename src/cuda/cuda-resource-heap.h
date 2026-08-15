#pragma once

#include "cuda-base.h"
#include "../resource-heap.h"

namespace rhi::cuda {

class ResourceHeapImpl : public ResourceHeap
{
public:
    ResourceHeapImpl(Device* device, const ResourceHeapDesc& desc);
    ~ResourceHeapImpl();

    Result init();

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;

    CUdeviceptr m_memory = 0;
    bool m_isHostMemory = false;
};

} // namespace rhi::cuda
