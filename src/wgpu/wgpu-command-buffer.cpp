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

Result CommandBufferImpl::beginResourcePass(IResourcePassEncoder** outEncoder)
{
    SLANG_RETURN_ON_FAIL(m_resourcePassEncoder.init(this));
    *outEncoder = &m_resourcePassEncoder;
    return SLANG_OK;
}

Result CommandBufferImpl::beginRenderPass(const RenderPassDesc& desc, IRenderPassEncoder** outEncoder)
{
    SLANG_RETURN_ON_FAIL(m_renderPassEncoder.init(this, desc));
    *outEncoder = &m_renderPassEncoder;
    return SLANG_OK;
}

Result CommandBufferImpl::beginComputePass(IComputePassEncoder** outEncoder)
{
    SLANG_RETURN_ON_FAIL(m_computePassEncoder.init(this));
    *outEncoder = &m_computePassEncoder;
    return SLANG_OK;
}

Result CommandBufferImpl::beginRayTracingPass(IRayTracingPassEncoder** outEncoder)
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
