#pragma once

#include "metal-base.h"
#include "../resource-heap.h"

namespace rhi::metal {

class ResourceHeapImpl : public ResourceHeap
{
public:
    ResourceHeapImpl(Device* device, const ResourceHeapDesc& desc);
    ~ResourceHeapImpl();

    Result init();

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;

    NS::SharedPtr<MTL::Heap> m_heap;
};

} // namespace rhi::metal
