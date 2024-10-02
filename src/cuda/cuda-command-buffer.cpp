#include "cuda-command-buffer.h"

namespace rhi::cuda {

ICommandBuffer* CommandBufferImpl::getInterface(const Guid& guid)
{
    if (guid == GUID::IID_ISlangUnknown || guid == GUID::IID_ICommandBuffer)
        return static_cast<ICommandBuffer*>(this);
    return nullptr;
}

void CommandBufferImpl::init(DeviceImpl* device, TransientResourceHeap* transientHeap)
{
    m_device = device;
    m_transientHeap = transientHeap;
}

Result CommandBufferImpl::beginResourcePass(IResourcePassEncoder** outEncoder)
{
    m_resourcePassEncoder.init(this);
    *outEncoder = &m_resourcePassEncoder;
    return SLANG_OK;
}

Result CommandBufferImpl::beginRenderPass(const RenderPassDesc& desc, IRenderPassEncoder** outEncoder)
{
    SLANG_UNUSED(desc);
    *outEncoder = nullptr;
    return SLANG_E_NOT_AVAILABLE;
}

Result CommandBufferImpl::beginComputePass(IComputePassEncoder** outEncoder)
{
    m_computePassEncoder.init(this);
    *outEncoder = &m_computePassEncoder;
    return SLANG_OK;
}

Result CommandBufferImpl::beginRayTracingPass(IRayTracingPassEncoder** outEncoder)
{
    *outEncoder = nullptr;
    return SLANG_E_NOT_AVAILABLE;
}

Result CommandBufferImpl::getNativeHandle(NativeHandle* outHandle)
{
    *outHandle = {};
    return SLANG_E_NOT_AVAILABLE;
}

} // namespace rhi::cuda
