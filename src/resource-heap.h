#pragma once

#include <slang-rhi.h>

#include "core/common.h"
#include "device-child.h"
#include "rhi-shared-fwd.h"

#include <vector>

namespace rhi {

inline ResourceHeapUsage getResourceHeapUsage(const BufferDesc&)
{
    return ResourceHeapUsage::Buffers;
}

inline ResourceHeapUsage getResourceHeapUsage(const TextureDesc& desc)
{
    if (is_set(desc.usage, TextureUsage::RenderTarget) || is_set(desc.usage, TextureUsage::DepthStencil))
        return ResourceHeapUsage::RtDsTextures;
    return ResourceHeapUsage::NonRtDsTextures;
}

inline bool isResourceHeapUsageCompatible(ResourceHeapUsage heapUsage, ResourceHeapUsage resourceUsage)
{
    return (heapUsage & resourceUsage) == resourceUsage;
}

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

    // IResourceHeap implementation
    virtual SLANG_NO_THROW const ResourceHeapDesc& SLANG_MCALL getDesc() override { return m_desc; }
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;

    virtual bool isCompatible(const ResourceMemoryRequirements& requirements) const;

public:
    ResourceHeapDesc m_desc;
    StructHolder m_descHolder;
    std::vector<ResourceMemoryRequirements> m_requirements;
};

void setResourceHeapCompatibility(
    ResourceMemoryRequirements& requirements,
    Device* device,
    uint64_t backendMask = ~uint64_t(0)
);
bool isResourceHeapCompatibilityFromDevice(const ResourceMemoryRequirements& requirements, const Device* device);
uint64_t getResourceHeapCompatibilityMask(const ResourceMemoryRequirements& requirements);
void resetResourceMemoryRequirements(ResourceMemoryRequirements* outRequirements);

Result validateResourceHeapDesc(Device* device, const ResourceHeapDesc& desc);

Result validateResourcePlacement(
    Device* device,
    IResourceHeap* resourceHeap,
    Offset offset,
    const ResourceMemoryRequirements& requirements
);

} // namespace rhi
