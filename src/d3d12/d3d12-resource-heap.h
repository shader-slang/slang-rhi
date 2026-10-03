#pragma once

#include "d3d12-base.h"
#include "../resource-heap.h"

namespace rhi::d3d12 {

class ResourceHeapImpl : public ResourceHeap
{
public:
    ResourceHeapImpl(Device* device, const ResourceHeapDesc& desc);
    ~ResourceHeapImpl();

    Result init();

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;

    ComPtr<ID3D12Heap> m_heap;
};

} // namespace rhi::d3d12
