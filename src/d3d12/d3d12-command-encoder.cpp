#include "d3d12-command-encoder.h"
#include "d3d12-command-buffer.h"
#include "d3d12-device.h"
#include "d3d12-helper-functions.h"
#include "d3d12-pipeline.h"
#include "d3d12-query.h"
#include "d3d12-shader-object.h"
#include "d3d12-shader-program.h"
#include "d3d12-shader-table.h"
#include "d3d12-texture.h"
#include "d3d12-transient-heap.h"
#include "d3d12-input-layout.h"
#include "d3d12-texture-view.h"
#include "d3d12-acceleration-structure.h"

#include "core/short_vector.h"

namespace rhi::d3d12 {

Result CommandEncoderImpl::init(DeviceImpl* device, CommandQueueImpl* queue, TransientResourceHeapImpl* transientHeap)
{
    m_device = device;
    m_transientHeap = transientHeap;
    if (!m_transientHeap)
    {
        SLANG_RETURN_ON_FAIL(m_device->createTransientResourceHeapImpl(
            ITransientResourceHeap::Flags::AllowResizing,
            4096,
            1024,
            1024,
            m_transientHeap.writeRef()
        ));
        m_transientHeap->breakStrongReferenceToDevice();
    }

    SLANG_RETURN_ON_FAIL(m_transientHeap->allocateCommandBuffer(m_commandBuffer.writeRef()));

    m_cmdList = static_cast<ID3D12GraphicsCommandList*>(m_commandBuffer->m_cmdList.get());
    m_cmdList->QueryInterface<ID3D12GraphicsCommandList1>(m_cmdList1.writeRef());
    m_cmdList->QueryInterface<ID3D12GraphicsCommandList4>(m_cmdList4.writeRef());
    m_cmdList->QueryInterface<ID3D12GraphicsCommandList6>(m_cmdList6.writeRef());

    return SLANG_OK;
}


Result CommandEncoderImpl::finish(ICommandBuffer** outCommandBuffer)
{
    if (!m_commandBuffer)
    {
        return SLANG_FAIL;
    }
    // endPassEncoder();

    // Transition all resources back to their default states.
    m_stateTracking.requireDefaultStates();
    commitBarriers();
    m_stateTracking.clear();

    SLANG_RETURN_ON_FAIL(m_cmdList->Close());
    returnComPtr(outCommandBuffer, m_commandBuffer);
    m_commandBuffer = nullptr;
    m_cmdList = nullptr;
    m_cmdList1 = nullptr;
    m_cmdList4 = nullptr;
    m_cmdList6 = nullptr;
    return SLANG_OK;
}

Result CommandEncoderImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::D3D12GraphicsCommandList;
    outHandle->value = (uint64_t)m_cmdList.get();
    return SLANG_OK;
}


Result CommandEncoderImpl::bindRootObject(
    RootShaderObjectImpl* rootObject,
    RootShaderObjectLayoutImpl* rootObjectLayout,
    Submitter* submitter
)
{
    // We need to set up a context for binding shader objects to the pipeline state.
    // This type mostly exists to bundle together a bunch of parameters that would
    // otherwise need to be tunneled down through all the shader object binding
    // logic.
    //
    BindingContext context = {};
    context.submitter = submitter;
    context.device = m_device;
    context.transientHeap = m_transientHeap;
    context.outOfMemoryHeap = (D3D12_DESCRIPTOR_HEAP_TYPE)(-1);

    // Transition all resources to the appropriate state and commit the barriers.
    // This needs to happen before binding descriptor tables, otherwise D3D12
    // will report validation errors.
    m_rootObject->setResourceStates(m_stateTracking);
    commitBarriers();

    // We kick off binding of shader objects at the root object, and the objects
    // themselves will be responsible for allocating, binding, and filling in
    // any descriptor tables or other root parameters needed.
    //
    bindDescriptorHeaps();
    if (rootObject->bindAsRoot(&context, rootObjectLayout) == SLANG_E_OUT_OF_MEMORY)
    {
        if (!m_transientHeap->canResize())
        {
            return SLANG_E_OUT_OF_MEMORY;
        }

        // If we run out of heap space while binding, allocate new descriptor heaps and try again.
        ID3D12DescriptorHeap* d3dheap = nullptr;
        invalidateDescriptorHeapBinding();
        switch (context.outOfMemoryHeap)
        {
        case D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV:
            SLANG_RETURN_ON_FAIL(m_transientHeap->allocateNewViewDescriptorHeap(m_device));
            d3dheap = m_transientHeap->getCurrentViewHeap().getHeap();
            bindDescriptorHeaps();
            break;
        case D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER:
            SLANG_RETURN_ON_FAIL(m_transientHeap->allocateNewSamplerDescriptorHeap(m_device));
            d3dheap = m_transientHeap->getCurrentSamplerHeap().getHeap();
            bindDescriptorHeaps();
            break;
        default:
            SLANG_RHI_ASSERT_FAILURE("Shouldn't be here");
            return SLANG_FAIL;
        }

        // Try again.
        SLANG_RETURN_ON_FAIL(rootObject->bindAsRoot(&context, rootObjectLayout));
    }

    return SLANG_OK;
}

void CommandEncoderImpl::bindDescriptorHeaps()
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


#if 0

// ResourcePassEncoderImpl


#endif

} // namespace rhi::d3d12


#if 0


void CommandBufferImpl::reinit()
{
    invalidateDescriptorHeapBinding();
    m_rootShaderObject.init(m_device);
}

#endif
