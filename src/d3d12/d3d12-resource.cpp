#include "d3d12-resource.h"

#include "core/string.h"

namespace rhi {

/* !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! D3D12BarrierSubmitter !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! */

void D3D12BarrierSubmitter::_flush()
{
    SLANG_RHI_ASSERT(m_numBarriers > 0);

    if (m_commandList)
    {
        m_commandList->ResourceBarrier(UINT(m_numBarriers), m_barriers);
    }
    m_numBarriers = 0;
}

D3D12_RESOURCE_BARRIER& D3D12BarrierSubmitter::_expandOne()
{
    _flush();
    return m_barriers[m_numBarriers++];
}

void D3D12BarrierSubmitter::transition(
    ID3D12Resource* resource,
    D3D12_RESOURCE_STATES prevState,
    D3D12_RESOURCE_STATES nextState
)
{
    if (nextState != prevState)
    {
        D3D12_RESOURCE_BARRIER& barrier = expandOne();

        const UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        const D3D12_RESOURCE_BARRIER_FLAGS flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;

        ::memset(&barrier, 0, sizeof(barrier));
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = flags;
        barrier.Transition.pResource = resource;
        barrier.Transition.StateBefore = prevState;
        barrier.Transition.StateAfter = nextState;
        barrier.Transition.Subresource = subresource;
    }
    else
    {
        if (nextState == D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
        {
            D3D12_RESOURCE_BARRIER& barrier = expandOne();

            ::memset(&barrier, 0, sizeof(barrier));
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            barrier.UAV.pResource = resource;
        }
    }
}

/* !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! D3D12ResourceBase !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! */

void D3D12ResourceBase::transition(
    D3D12_RESOURCE_STATES oldState,
    D3D12_RESOURCE_STATES nextState,
    D3D12BarrierSubmitter& submitter
)
{
    // Transition only if there is a resource
    if (m_resource && oldState != nextState)
    {
        submitter.transition(m_resource, oldState, nextState);
    }
}

/* !!!!!!!!!!!!!!!!!!!!!!!!! D3D12Resource !!!!!!!!!!!!!!!!!!!!!!!! */

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
    if (resource != m_resource)
    {
        if (resource)
        {
            resource->AddRef();
        }
        if (m_resource)
        {
            m_resource->Release();
        }
        m_resource = resource;
    }
}

void D3D12Resource::setResourceNull()
{
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
    const D3D12_CLEAR_VALUE* clearValue
)
{
    setResourceNull();
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

ID3D12Resource* D3D12Resource::detach()
{
    ID3D12Resource* resource = m_resource;
    m_resource = nullptr;
    return resource;
}

void D3D12Resource::swap(ComPtr<ID3D12Resource>& resourceInOut)
{
    ID3D12Resource* tmp = m_resource;
    m_resource = resourceInOut.detach();
    resourceInOut.attach(tmp);
}

} // namespace rhi
