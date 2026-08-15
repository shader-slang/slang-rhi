#include "metal-resource-heap.h"
#include "metal-device.h"
#include "metal-utils.h"

namespace rhi::metal {

static MTL::StorageMode translateStorageMode(MemoryType memoryType)
{
    switch (memoryType)
    {
    case MemoryType::Upload:
    case MemoryType::ReadBack:
        return MTL::StorageModeShared;
    default:
        return MTL::StorageModePrivate;
    }
}

ResourceHeapImpl::ResourceHeapImpl(Device* device, const ResourceHeapDesc& desc)
    : ResourceHeap(device, desc)
{
}

ResourceHeapImpl::~ResourceHeapImpl() {}

Result ResourceHeapImpl::init()
{
    AUTORELEASEPOOL

    DeviceImpl* device = getDevice<DeviceImpl>();
    NS::SharedPtr<MTL::HeapDescriptor> heapDesc = NS::TransferPtr(MTL::HeapDescriptor::alloc()->init());
    heapDesc->setType(MTL::HeapTypePlacement);
    heapDesc->setSize(m_desc.size);
    heapDesc->setStorageMode(translateStorageMode(m_desc.memoryType));
    heapDesc->setHazardTrackingMode(MTL::HazardTrackingModeUntracked);

    m_heap = NS::TransferPtr(device->m_device->newHeap(heapDesc.get()));
    if (!m_heap)
        return SLANG_FAIL;

    m_desc.size = m_heap->size();
    if (m_desc.label)
        m_heap->setLabel(createString(m_desc.label).get());

    return SLANG_OK;
}

Result ResourceHeapImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::MTLHeap;
    outHandle->value = (uint64_t)m_heap.get();
    return SLANG_OK;
}

} // namespace rhi::metal
