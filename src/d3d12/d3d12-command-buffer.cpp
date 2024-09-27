#include "d3d12-command-buffer.h"
#include "d3d12-transient-heap.h"

namespace rhi::d3d12 {

// There are a pair of cyclic references between a `TransientResourceHeap` and
// a `CommandBuffer` created from the heap. We need to break the cycle upon
// the public reference count of a command buffer dropping to 0.

ICommandBufferD3D12* CommandBufferImpl::getInterface(const Guid& guid)
{
    if (guid == GUID::IID_ISlangUnknown || guid == GUID::IID_ICommandBuffer || guid == GUID::IID_ICommandBufferD3D12)
        return static_cast<ICommandBufferD3D12*>(this);
    return nullptr;
}

Result CommandBufferImpl::getNativeHandle(NativeHandle* handle)
{
    handle->type = NativeHandleType::D3D12GraphicsCommandList;
    handle->value = (uint64_t)m_cmdList.get();
    return SLANG_OK;
}

void CommandBufferImpl::requireBufferState(BufferImpl* buffer, ResourceState state)
{
    m_stateTracking.setBufferState(buffer, state);
}

void CommandBufferImpl::requireTextureState(TextureImpl* texture, ResourceState state)
{
    m_stateTracking.setTextureState(texture, state);
}

void CommandBufferImpl::commitBarriers()
{
    short_vector<D3D12_RESOURCE_BARRIER, 16> barriers;

    for (const auto& bufferBarrier : m_stateTracking.getBufferBarriers())
    {
        BufferImpl* buffer = static_cast<BufferImpl*>(bufferBarrier.buffer);
        D3D12_RESOURCE_BARRIER barrier = {};
        bool isUAVBarrier =
            (bufferBarrier.stateBefore == bufferBarrier.stateAfter &&
             bufferBarrier.stateAfter == ResourceState::UnorderedAccess);
        if (isUAVBarrier)
        {
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            barrier.UAV.pResource = buffer->m_resource;
        }
        else
        {
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = buffer->m_resource;
            barrier.Transition.StateBefore = D3DUtil::getResourceState(bufferBarrier.stateBefore);
            barrier.Transition.StateAfter = D3DUtil::getResourceState(bufferBarrier.stateAfter);
            barrier.Transition.Subresource = 0;
        }
        barriers.push_back(barrier);
    }

    for (const auto& textureBarrier : m_stateTracking.getTextureBarriers())
    {
        TextureImpl* texture = static_cast<TextureImpl*>(textureBarrier.texture);
        D3D12_RESOURCE_BARRIER barrier = {};
        bool isUAVBarrier =
            (textureBarrier.stateBefore == textureBarrier.stateAfter &&
             textureBarrier.stateAfter == ResourceState::UnorderedAccess);
        if (isUAVBarrier)
        {
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            barrier.UAV.pResource = texture->m_resource;
        }
        else
        {
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = texture->m_resource;
            barrier.Transition.StateBefore = D3DUtil::getResourceState(textureBarrier.stateBefore);
            barrier.Transition.StateAfter = D3DUtil::getResourceState(textureBarrier.stateAfter);
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        }
        barriers.push_back(barrier);
    }

    if (!barriers.empty())
    {
        m_cmdList->ResourceBarrier((UINT)barriers.size(), barriers.data());
    }

    m_stateTracking.clearBarriers();
}

void CommandBufferImpl::bindDescriptorHeaps()
{
    if (!m_descriptorHeapsBound)
    {
        ID3D12DescriptorHeap* heaps[] = {
            m_transientHeap->getCurrentViewHeap().getHeap(),
            m_transientHeap->getCurrentSamplerHeap().getHeap(),
        };
        m_cmdList->SetDescriptorHeaps(SLANG_COUNT_OF(heaps), heaps);
        m_descriptorHeapsBound = true;
    }
}

void CommandBufferImpl::reinit()
{
    invalidateDescriptorHeapBinding();
    m_rootShaderObject.init(m_device);
}

void CommandBufferImpl::init(
    DeviceImpl* device,
    ID3D12GraphicsCommandList* d3dCommandList,
    TransientResourceHeapImpl* transientHeap
)
{
    m_transientHeap = transientHeap;
    m_device = device;
    m_cmdList = d3dCommandList;

    reinit();

    m_cmdList->QueryInterface<ID3D12GraphicsCommandList6>(m_cmdList6.writeRef());
    if (m_cmdList6)
    {
        m_cmdList4 = m_cmdList6;
        m_cmdList1 = m_cmdList6;
        return;
    }
#if SLANG_RHI_DXR
    m_cmdList->QueryInterface<ID3D12GraphicsCommandList4>(m_cmdList4.writeRef());
    if (m_cmdList4)
    {
        m_cmdList1 = m_cmdList4;
        return;
    }
#endif
    m_cmdList->QueryInterface<ID3D12GraphicsCommandList1>(m_cmdList1.writeRef());
}

Result CommandBufferImpl::encodeResourceCommands(IResourceCommandEncoder** outEncoder)
{
    m_resourceCommandEncoder.init(this);
    *outEncoder = &m_resourceCommandEncoder;
    return SLANG_OK;
}

Result CommandBufferImpl::encodeRenderCommands(const RenderPassDesc& desc, IRenderCommandEncoder** outEncoder)
{
    m_renderCommandEncoder.init(m_device, m_transientHeap, this, desc);
    *outEncoder = &m_renderCommandEncoder;
    return SLANG_OK;
}

Result CommandBufferImpl::encodeComputeCommands(IComputeCommandEncoder** outEncoder)
{
    m_computeCommandEncoder.init(m_device, m_transientHeap, this);
    *outEncoder = &m_computeCommandEncoder;
    return SLANG_OK;
}

Result CommandBufferImpl::encodeRayTracingCommands(IRayTracingCommandEncoder** outEncoder)
{
#if SLANG_RHI_DXR
    m_rayTracingCommandEncoder.init(this);
    *outEncoder = &m_rayTracingCommandEncoder;
    return SLANG_OK;
#else
    *outEncoder = nullptr;
    return SLANG_E_NOT_AVAILABLE;
#endif
}

void CommandBufferImpl::close()
{
    // Transition all resources back to their default states.
    m_stateTracking.requireDefaultStates();
    commitBarriers();
    m_stateTracking.clear();

    m_cmdList->Close();
}

} // namespace rhi::d3d12
