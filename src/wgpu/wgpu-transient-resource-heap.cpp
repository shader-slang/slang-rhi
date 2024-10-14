#include "wgpu-transient-resource-heap.h"
#include "wgpu-device.h"
#include "wgpu-command-buffer.h"
#include "wgpu-pipeline.h"

namespace rhi::wgpu {

Result TransientResourceHeapImpl::init(const ITransientResourceHeap::Desc& desc, DeviceImpl* device)
{
    Super::init(
        desc,
        256, // TODO
        device
    );

    return SLANG_OK;
}

TransientResourceHeapImpl::~TransientResourceHeapImpl() {}

Result TransientResourceHeapImpl::synchronizeAndReset()
{
    Super::reset();
    return SLANG_OK;
}

Result DeviceImpl::createTransientResourceHeap(
    const ITransientResourceHeap::Desc& desc,
    ITransientResourceHeap** outHeap
)
{
    RefPtr<TransientResourceHeapImpl> heap = new TransientResourceHeapImpl();
    SLANG_RETURN_ON_FAIL(heap->init(desc, this));
    returnComPtr(outHeap, heap);
    return SLANG_OK;
}

} // namespace rhi::wgpu
