#pragma once

#include <slang-rhi.h>

#include "core/common.h"
#include "device-child.h"
#include "rhi-shared-fwd.h"

namespace rhi {

class ResourceHeap : public IResourceHeap, public DeviceChild
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL

    IResourceHeap* getInterface(const Guid& guid)
    {
        if (guid == ISlangUnknown::getTypeGuid() || guid == IResourceHeap::getTypeGuid())
            return static_cast<IResourceHeap*>(this);
        return nullptr;
    }

    ResourceHeap(Device* device, const ResourceHeapDesc& desc);
    virtual ~ResourceHeap() = default;

    virtual void makeExternal() override { establishStrongReferenceToDevice(); }
    virtual void makeInternal() override { breakStrongReferenceToDevice(); }

    // IResourceHeap implementation
    virtual SLANG_NO_THROW const ResourceHeapDesc& SLANG_MCALL getDesc() override { return m_desc; }
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;

public:
    ResourceHeapDesc m_desc;
    StructHolder m_descHolder;
};

const ResourcePlacementDesc* findResourcePlacementDesc(const void* next);

Result validateResourcePlacement(
    const ResourcePlacementDesc& placement,
    const ResourceMemoryRequirements& requirements
);

} // namespace rhi
