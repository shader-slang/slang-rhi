#include "cuda-command-buffer.h"

namespace rhi::cuda {

ICommandBuffer* CommandBufferImpl::getInterface(const Guid& guid)
{
    if (guid == GUID::IID_ISlangUnknown || guid == GUID::IID_ICommandBuffer)
        return static_cast<ICommandBuffer*>(this);
    return nullptr;
}

void CommandBufferImpl::init(DeviceImpl* device, TransientResourceHeapBase* transientHeap)
{
    m_device = device;
    m_transientHeap = transientHeap;
}

SLANG_NO_THROW Result SLANG_MCALL CommandBufferImpl::encodeResourceCommands(IResourceCommandEncoder** outEncoder)
{
    m_resourceCommandEncoder.init(this);
    *outEncoder = &m_resourceCommandEncoder;
    return SLANG_OK;
}

SLANG_NO_THROW Result SLANG_MCALL
CommandBufferImpl::encodeRenderCommands(const RenderPassDesc& desc, IRenderCommandEncoder** outEncoder)
{
    SLANG_UNUSED(desc);
    *outEncoder = nullptr;
    return SLANG_E_NOT_AVAILABLE;
}

SLANG_NO_THROW Result SLANG_MCALL CommandBufferImpl::encodeComputeCommands(IComputeCommandEncoder** outEncoder)
{
    m_computeCommandEncoder.init(this);
    *outEncoder = &m_computeCommandEncoder;
    return SLANG_OK;
}

SLANG_NO_THROW Result SLANG_MCALL CommandBufferImpl::encodeRayTracingCommands(IRayTracingCommandEncoder** outEncoder)
{
    *outEncoder = nullptr;
    return SLANG_E_NOT_AVAILABLE;
}

SLANG_NO_THROW Result SLANG_MCALL CommandBufferImpl::getNativeHandle(NativeHandle* outHandle)
{
    *outHandle = {};
    return SLANG_E_NOT_AVAILABLE;
}

} // namespace rhi::cuda
