#pragma once

#include <slang-rhi.h>

#include "core/common.h"
#include "core/offset-allocator.h"

#include "device-child.h"

#include "rhi-shared-fwd.h"


namespace rhi {

class GraphicsHeap : public IGraphicsHeap, public DeviceChild
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL

    IGraphicsHeap* getInterface(const Guid& guid)
    {
        if (guid == ISlangUnknown::getTypeGuid() || guid == IGraphicsHeap::getTypeGuid())
            return static_cast<IGraphicsHeap*>(this);
        return nullptr;
    }

public:
    struct PageDesc
    {
        Size alignment = 0;
        Size size = 0;
    };

    class Page
    {
    public:
        Page(GraphicsHeap* heap, const PageDesc& desc)
            : m_id(0)
            , m_heap(heap)
            , m_desc(desc)
            , m_allocator(desc.size / desc.alignment, desc.size / desc.alignment)
        {
        }

        uint32_t m_id;
        GraphicsHeap* m_heap;
        PageDesc m_desc;
        OffsetAllocator m_allocator;
    };


    GraphicsHeap(Device* device, const GraphicsHeapDesc& desc)
        : DeviceChild(device)
    {
        m_desc = desc;
    }

    virtual void makeExternal() override { establishStrongReferenceToDevice(); }
    virtual void makeInternal() override { breakStrongReferenceToDevice(); }

    // Generally the allocate is common to all platforms, as it's the page allocation
    // that is platform specific. However freeing depends on pipeline state so is
    // platform specific.
    virtual SLANG_NO_THROW Result SLANG_MCALL allocate(
        const GraphicsAllocDesc& desc,
        GraphicsAllocation* allocation
    ) override;

    Result createPage(const PageDesc& desc, Page** page);
    Result destroyPage(Page* page);

    Result cleanUp();

    // Device implementation should provide these
    virtual Result allocatePage(const PageDesc& desc, Page** page) = 0;
    virtual Result freePage(Page* page) = 0;

    // Device implementation should call this when a freed allocation can be returned to the pool
    Result retire(GraphicsAllocation allocation);

public:
    GraphicsHeapDesc m_desc;
    uint32_t m_nextPageId = 1;

    std::vector<Page*> m_pages;
};

} // namespace rhi
