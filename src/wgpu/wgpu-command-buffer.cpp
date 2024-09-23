#include "wgpu-command-buffer.h"
#include "wgpu-device.h"
#include "wgpu-transient-resource-heap.h"

namespace rhi::wgpu {

ICommandBuffer* CommandBufferImpl::getInterface(const Guid& guid)
{
    if (guid == GUID::IID_ISlangUnknown || guid == GUID::IID_ICommandBuffer)
        return static_cast<ICommandBuffer*>(this);
    return nullptr;
}

CommandBufferImpl::~CommandBufferImpl() {}

Result CommandBufferImpl::encodeResourceCommands(IResourceCommandEncoder** outEncoder)
{
    SLANG_RETURN_ON_FAIL(m_resourceCommandEncoder.init(this));
    *outEncoder = &m_resourceCommandEncoder;
    return SLANG_OK;
}

Result CommandBufferImpl::encodeRenderCommands(const RenderPassDesc& desc, IRenderCommandEncoder** outEncoder)
{
    SLANG_RETURN_ON_FAIL(m_renderCommandEncoder.init(this, desc));
    *outEncoder = &m_renderCommandEncoder;
    return SLANG_OK;
}

Result CommandBufferImpl::encodeComputeCommands(IComputeCommandEncoder** outEncoder)
{
    SLANG_RETURN_ON_FAIL(m_computeCommandEncoder.init(this));
    *outEncoder = &m_computeCommandEncoder;
    return SLANG_OK;
}

Result CommandBufferImpl::encodeRayTracingCommands(IRayTracingCommandEncoder** outEncoder)
{
    return SLANG_E_NOT_AVAILABLE;
}

void CommandBufferImpl::close()
{
    m_commandBuffer = m_device->m_ctx.api.wgpuCommandEncoderFinish(m_commandEncoder, nullptr);
}

Result CommandBufferImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::WGPUCommandBuffer;
    outHandle->value = (uint64_t)m_commandBuffer;
    return SLANG_OK;
}

} // namespace rhi::wgpu
