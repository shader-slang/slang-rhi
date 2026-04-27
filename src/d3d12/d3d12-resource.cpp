#include "d3d12-resource.h"

#include "core/string.h"

namespace rhi {

/// Returns true for DXGI formats that use planar memory layout.
/// D3D12 does not support small resource placement alignment (4096) for these formats.
static bool isPlanarFormat(DXGI_FORMAT format)
{
    switch (format)
    {
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
    case DXGI_FORMAT_R32G8X24_TYPELESS:
    case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
    case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
    case DXGI_FORMAT_D24_UNORM_S8_UINT:
    case DXGI_FORMAT_R24G8_TYPELESS:
    case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
    case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
    case DXGI_FORMAT_NV12:
    case DXGI_FORMAT_P010:
    case DXGI_FORMAT_P016:
    case DXGI_FORMAT_420_OPAQUE:
    case DXGI_FORMAT_NV11:
        return true;
    default:
        return false;
    }
}

void D3D12Resource::setDebugName(ID3D12Resource* resource, const char* name)
{
    if (resource)
    {
        resource->SetName(string::to_wstring(name).data());
    }
}

void D3D12Resource::setDebugName(const char* name)
{
    setDebugName(m_resource, name);
}

void D3D12Resource::setDebugName(const wchar_t* name)
{
    if (m_resource)
    {
        m_resource->SetName(name);
    }
}

void D3D12Resource::setResource(ID3D12Resource* resource)
{
    if (resource == m_resource && !m_allocation)
    {
        return;
    }

    setResourceNull();

    if (resource)
    {
        resource->AddRef();
        m_resource = resource;
    }
}

void D3D12Resource::setResourceNull()
{
    if (m_allocation)
    {
        m_allocation.setNull();
        m_resource = nullptr;
        return;
    }

    if (m_resource)
    {
        m_resource->Release();
        m_resource = nullptr;
    }
}

Result D3D12Resource::initCommitted(
    ID3D12Device* device,
    const D3D12_HEAP_PROPERTIES& heapProps,
    D3D12_HEAP_FLAGS heapFlags,
    const D3D12_RESOURCE_DESC& resourceDesc,
    D3D12_RESOURCE_STATES initState,
    const D3D12_CLEAR_VALUE* clearValue,
    D3D12MA::Allocator* allocator
)
{
    setResourceNull();

    const bool canUseAllocator =
        allocator &&
        (heapProps.Type == D3D12_HEAP_TYPE_DEFAULT || heapProps.Type == D3D12_HEAP_TYPE_UPLOAD ||
         heapProps.Type == D3D12_HEAP_TYPE_READBACK) &&
        heapProps.CPUPageProperty == D3D12_CPU_PAGE_PROPERTY_UNKNOWN &&
        heapProps.MemoryPoolPreference == D3D12_MEMORY_POOL_UNKNOWN && heapProps.CreationNodeMask == 1 &&
        heapProps.VisibleNodeMask == 1;

    if (canUseAllocator)
    {
        D3D12MA::ALLOCATION_DESC allocDesc = {};
        allocDesc.HeapType = heapProps.Type;
        allocDesc.ExtraHeapFlags = heapFlags;

        // Shared resources must be committed (not sub-allocated).
        if (heapFlags & D3D12_HEAP_FLAG_SHARED)
            allocDesc.Flags = D3D12MA::ALLOCATION_FLAGS(allocDesc.Flags | D3D12MA::ALLOCATION_FLAG_COMMITTED);

        // D3D12MA may probe small resource placement alignment (4096) for textures with Alignment==0.
        // Planar formats (e.g. D32_FLOAT_S8X24_UINT) don't support small alignment, and the probe
        // triggers a D3D12 debug layer error. Set default alignment explicitly to skip the probe.
        D3D12_RESOURCE_DESC desc = resourceDesc;
        if (desc.Alignment == 0 && isPlanarFormat(desc.Format))
            desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;

        SLANG_RETURN_ON_FAIL(
            allocator
                ->CreateResource(&allocDesc, &desc, initState, clearValue, m_allocation.writeRef(), IID_NULL, nullptr)
        );

        m_resource = m_allocation->GetResource();
        return SLANG_OK;
    }

    ComPtr<ID3D12Resource> resource;
    SLANG_RETURN_ON_FAIL(device->CreateCommittedResource(
        &heapProps,
        heapFlags,
        &resourceDesc,
        initState,
        clearValue,
        IID_PPV_ARGS(resource.writeRef())
    ));
    setResource(resource);
    return SLANG_OK;
}

} // namespace rhi
