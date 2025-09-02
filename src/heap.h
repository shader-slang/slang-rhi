#pragma once

#include <slang-rhi.h>

#include "core/common.h"
#include "core/offset-allocator.h"

#include "device-child.h"

#include "rhi-shared-fwd.h"

#include <vector>

namespace rhi {

class Heap : public IHeap, public DeviceChild
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL

    IHeap* getInterface(const Guid& guid)
    {
        if (guid == ISlangUnknown::getTypeGuid() || guid == IHeap::getTypeGuid())
            return static_cast<IHeap*>(this);
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
        Page(Heap* heap, const PageDesc& desc)
            : m_id(0)
            , m_heap(heap)
            , m_desc(desc)
            , m_allocator(desc.size / desc.alignment, desc.size / desc.alignment)
        {
        }

        virtual ~Page() {}

        virtual DeviceAddress offsetToAddress(Size offset) = 0;

        uint32_t m_id;
        Heap* m_heap;
        PageDesc m_desc;
        OffsetAllocator m_allocator;
    };


    Heap(Device* device, const HeapDesc& desc)
        : DeviceChild(device)
    {
        m_desc = desc;
        m_descHolder.holdString(m_desc.label);
    }

    virtual void makeExternal() override { establishStrongReferenceToDevice(); }
    virtual void makeInternal() override { breakStrongReferenceToDevice(); }

    // Generally the allocate is common to all platforms, as it's the page allocation
    // that is platform specific. However freeing depends on pipeline state so is
    // platform specific.
    virtual SLANG_NO_THROW Result SLANG_MCALL allocate(const HeapAllocDesc& desc, HeapAlloc* outAllocation) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL report(HeapReport* outReport) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL removeEmptyPages() override;

    Result createPage(const PageDesc& desc, Page** outPage);
    Result destroyPage(Page* page);

    // Device implementation should provide these
    virtual Result allocatePage(const PageDesc& desc, Page** outPage) = 0;
    virtual Result freePage(Page* page) = 0;

    // Device implementation can use to enforce alignments/sizes
    virtual Result fixUpAllocDesc(HeapAllocDesc& desc)
    {
        // Default implementation does nothing
        return SLANG_OK;
    }

    // Device implementation should call this when a freed allocation can be returned to the pool
    Result retire(HeapAlloc allocation);

public:
    HeapDesc m_desc;
    StructHolder m_descHolder;
    uint32_t m_nextPageId = 1;


    std::vector<Page*> m_pages;
};

} // namespace rhi
