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

        /// Stream context for this page (backend-specific handle).
        /// Passed from HeapAllocDesc when creating the page.
        void* stream = kNoStream;
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

        // ============================================================================
        // Stream tracking for caching allocator (PyTorch-style)
        // ============================================================================

        /// The stream this page was originally allocated on.
        /// This NEVER changes - ownership remains with original stream (PyTorch model).
        /// Backend-specific handle (CUstream for CUDA, queue handle for D3D/Vk).
        /// Set to kNoStream if allocated outside encoding context (lazy assignment on first use).
        void* m_stream = kNoStream;

        /// Record that this page is being used by a stream different from m_stream.
        /// Backend implementations override this to insert synchronization events.
        /// Following PyTorch model: events are only added when stream != m_stream.
        /// @param stream The stream using this page (backend-specific handle)
        virtual void recordStreamUse(void* stream)
        {
            // Default: no-op, backend overrides with event insertion
            (void)stream;
        }

        /// Check if this page can be reused (all pending stream events completed).
        /// Backend implementations override this to query event completion status.
        /// @return true if the page can be safely reused
        virtual bool canReuse() const
        {
            // Default: always reusable (no cross-stream tracking)
            return true;
        }

        /// Process completed events and clean up.
        /// Called periodically or before reuse checks.
        /// Backend implementations override to query and remove completed events.
        virtual void processEvents()
        {
            // Default: no-op
        }

        /// Called when this page is used for an allocation.
        /// Backend implementations override to track cross-stream usage.
        /// This enables proper multi-stream synchronization: if a page allocated
        /// on stream A is used during encoding for stream B, we record that usage.
        /// @param stream The stream context for this allocation (from HeapAllocDesc::stream)
        virtual void notifyUse(void* stream)
        {
            // Default: no-op, backend overrides with stream tracking
            (void)stream;
        }

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
