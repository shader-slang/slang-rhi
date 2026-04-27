#pragma once

#include "d3d12-api.h"
#include "../d3d/d3d-utils.h"

#include <slang-com-ptr.h>

namespace rhi {

struct D3D12Resource
{
    /// Get the associated resource
    SLANG_FORCE_INLINE ID3D12Resource* getResource() const { return m_resource; }

    /// Coercible into ID3D12Resource
    SLANG_FORCE_INLINE operator ID3D12Resource*() const { return m_resource; }

    D3D12Resource() = default;
    ~D3D12Resource() { setResourceNull(); }

    /// Initialize as committed resource
    Result initCommitted(
        ID3D12Device* device,
        const D3D12_HEAP_PROPERTIES& heapProps,
        D3D12_HEAP_FLAGS heapFlags,
        const D3D12_RESOURCE_DESC& resourceDesc,
        D3D12_RESOURCE_STATES initState,
        const D3D12_CLEAR_VALUE* clearValue,
        D3D12MA::Allocator* allocator = nullptr
    );

    /// Set a resource.
    void setResource(ID3D12Resource* resource);
    /// Make the resource null
    void setResourceNull();

    /// Set the debug name on a resource
    static void setDebugName(ID3D12Resource* resource, const char* name);

    /// Set the the debug name on the resource
    void setDebugName(const wchar_t* name);
    /// Set the debug name
    void setDebugName(const char* name);

    ComPtr<D3D12MA::Allocation> m_allocation;
    ID3D12Resource* m_resource = nullptr;
};

} // namespace rhi
