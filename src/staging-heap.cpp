#include "staging-heap.h"

#include "rhi-shared.h"
#include "device.h"

namespace rhi
{

void StagingHeap::initialize(Device* device)
{
    m_device = device;
}

RefPtr<StagingHeap::Handle> StagingHeap::allocHandle(size_t size, size_t alignment, MetaData metadata)
{
    Allocation allocation = alloc(size, alignment);
    return new Handle(this, allocation);
}

StagingHeap::Allocation StagingHeap::alloc(size_t size, size_t alignment)
{
    RefPtr<Page> page = allocPage(size);

    Allocation res;
    res.page = page->getId();
    res.flags = 0;
    res.buffer = page->getBuffer();
    return res;
}

void StagingHeap::free(Allocation allocation)
{

}

RefPtr<StagingHeap::Page> StagingHeap::allocPage(size_t size)
{
    ComPtr<IBuffer> bufferPtr;
    BufferDesc bufferDesc;
    bufferDesc.usage = BufferUsage::CopyDestination | BufferUsage::CopySource;
    bufferDesc.defaultState = ResourceState::General;
    bufferDesc.memoryType = MemoryType::Upload;
    bufferDesc.size = size;

    //TODO(staging-heap): Check for failure
    m_device->createBuffer(bufferDesc, nullptr, bufferPtr.writeRef());

    //Create page and store buffer pointer
    RefPtr<Page> page = new Page(m_next_page_id++, checked_cast<Buffer*>(bufferPtr.get()));
    m_pages.insert({page->getId(), page});

    //TODO(staging-heap): Check wtf this is
    // The buffer is owned by the page.
    page->getBuffer()->comFree();

    return page;
}

}
