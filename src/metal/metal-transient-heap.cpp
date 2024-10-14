#include "metal-transient-heap.h"
#include "metal-device.h"
#include "metal-util.h"

namespace rhi::metal {

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

} // namespace rhi::metal
