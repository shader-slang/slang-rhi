#include "d3d12-resource-heap.h"
#include "d3d12-device.h"
#include "d3d12-utils.h"
#include "core/string.h"

namespace rhi::d3d12 {

static D3D12_HEAP_TYPE translateHeapType(MemoryType memoryType)
{
    switch (memoryType)
    {
    case MemoryType::Upload:
        return D3D12_HEAP_TYPE_UPLOAD;
    case MemoryType::ReadBack:
        return D3D12_HEAP_TYPE_READBACK;
    default:
        return D3D12_HEAP_TYPE_DEFAULT;
    }
}

static D3D12_HEAP_FLAGS translateHeapFlags(ResourceHeapKind kind)
{
    switch (kind)
    {
    case ResourceHeapKind::Buffers:
        return D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
    case ResourceHeapKind::NonRtDsTextures:
        return D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES;
    case ResourceHeapKind::RtDsTextures:
        return D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES;
    case ResourceHeapKind::All:
    default:
        return D3D12_HEAP_FLAG_NONE;
    }
}

ResourceHeapImpl::ResourceHeapImpl(Device* device, const ResourceHeapDesc& desc)
    : ResourceHeap(device, desc)
{
}

ResourceHeapImpl::~ResourceHeapImpl() {}

Result ResourceHeapImpl::init()
{
    DeviceImpl* device = getDevice<DeviceImpl>();

    if (m_desc.kind == ResourceHeapKind::All && device->m_resourceHeapTier < D3D12_RESOURCE_HEAP_TIER_2)
        return SLANG_E_NOT_AVAILABLE;

    const Size alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    m_desc.size = math::calcAligned(m_desc.size, alignment);

    D3D12_HEAP_DESC heapDesc = {};
    heapDesc.SizeInBytes = m_desc.size;
    heapDesc.Properties = makeHeapProperties(translateHeapType(m_desc.memoryType));
    heapDesc.Alignment = alignment;
    heapDesc.Flags = translateHeapFlags(m_desc.kind);

    SLANG_D3D_RETURN_ON_FAIL_REPORT(device->m_device->CreateHeap(&heapDesc, IID_PPV_ARGS(m_heap.writeRef())), device);

    if (m_desc.label)
        m_heap->SetName(string::to_wstring(m_desc.label).c_str());

    return SLANG_OK;
}

Result ResourceHeapImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::D3D12Heap;
    outHandle->value = (uint64_t)m_heap.get();
    return SLANG_OK;
}

} // namespace rhi::d3d12
