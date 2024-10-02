#include "debug-command-buffer.h"
#include "debug-helper-functions.h"

namespace rhi::debug {

DebugCommandBuffer::DebugCommandBuffer()
{
    SLANG_RHI_API_FUNC;
    m_renderPassEncoder.commandBuffer = this;
    m_computePassEncoder.commandBuffer = this;
    m_resourcePassEncoder.commandBuffer = this;
    m_rayTracingPassEncoder.commandBuffer = this;
}

ICommandBuffer* DebugCommandBuffer::getInterface(const Guid& guid)
{
    if (guid == GUID::IID_ICommandBuffer || guid == GUID::IID_ISlangUnknown)
        return (DebugObject<ICommandBuffer>*)this;
    if (guid == GUID::IID_ICommandBufferD3D12)
        return static_cast<ICommandBufferD3D12*>(this);
    return nullptr;
}

Result DebugCommandBuffer::beginResourcePass(IResourcePassEncoder** outEncoder)
{
    SLANG_RHI_API_FUNC;
    checkCommandBufferOpenWhenCreatingEncoder();
    checkEncodersClosedBeforeNewEncoder();
    m_resourcePassEncoder.isOpen = true;
    SLANG_RETURN_ON_FAIL(baseObject->beginResourcePass(&m_resourcePassEncoder.baseObject));
    *outEncoder = &m_resourcePassEncoder;
    return SLANG_OK;
}

Result DebugCommandBuffer::beginRenderPass(const RenderPassDesc& desc, IRenderPassEncoder** outEncoder)
{
    // TODO VALIDATION: resolveTarget must have usage RenderTarget (Vulkan, WGPU)

    SLANG_RHI_API_FUNC;
    checkCommandBufferOpenWhenCreatingEncoder();
    checkEncodersClosedBeforeNewEncoder();
    RenderPassDesc innerDesc = desc;
    short_vector<RenderPassColorAttachment> innerColorAttachments;
    for (Index i = 0; i < desc.colorAttachmentCount; ++i)
    {
        innerColorAttachments.push_back(desc.colorAttachments[i]);
        innerColorAttachments[i].view = getInnerObj(desc.colorAttachments[i].view);
        innerColorAttachments[i].resolveTarget = getInnerObj(desc.colorAttachments[i].resolveTarget);
    }
    innerDesc.colorAttachments = innerColorAttachments.data();
    RenderPassDepthStencilAttachment innerDepthStencilAttachment;
    if (desc.depthStencilAttachment)
    {
        innerDepthStencilAttachment = *desc.depthStencilAttachment;
        innerDepthStencilAttachment.view = getInnerObj(desc.depthStencilAttachment->view);
        innerDesc.depthStencilAttachment = &innerDepthStencilAttachment;
    }
    m_renderPassEncoder.isOpen = true;
    SLANG_RETURN_ON_FAIL(baseObject->beginRenderPass(innerDesc, &m_renderPassEncoder.baseObject));
    *outEncoder = &m_renderPassEncoder;
    return SLANG_OK;
}

Result DebugCommandBuffer::beginComputePass(IComputePassEncoder** outEncoder)
{
    SLANG_RHI_API_FUNC;
    checkCommandBufferOpenWhenCreatingEncoder();
    checkEncodersClosedBeforeNewEncoder();
    m_computePassEncoder.isOpen = true;
    SLANG_RETURN_ON_FAIL(baseObject->beginComputePass(&m_computePassEncoder.baseObject));
    *outEncoder = &m_computePassEncoder;
    return SLANG_OK;
}

Result DebugCommandBuffer::beginRayTracingPass(IRayTracingPassEncoder** outEncoder)
{
    SLANG_RHI_API_FUNC;
    checkCommandBufferOpenWhenCreatingEncoder();
    checkEncodersClosedBeforeNewEncoder();
    m_rayTracingPassEncoder.isOpen = true;
    SLANG_RETURN_ON_FAIL(baseObject->beginRayTracingPass(&m_rayTracingPassEncoder.baseObject));
    *outEncoder = &m_rayTracingPassEncoder;
    return SLANG_OK;
}

void DebugCommandBuffer::close()
{
    SLANG_RHI_API_FUNC;
    if (!isOpen)
    {
        RHI_VALIDATION_ERROR("command buffer is already closed.");
    }
    if (m_renderPassEncoder.isOpen)
    {
        RHI_VALIDATION_ERROR(
            "A render pass encoder on this command buffer is still open. "
            "IRenderPassEncoder::end() must be called before closing a command buffer."
        );
    }
    if (m_computePassEncoder.isOpen)
    {
        RHI_VALIDATION_ERROR(
            "A compute pass encoder on this command buffer is still open. "
            "IComputePassEncoder::end() must be called before closing a command buffer."
        );
    }
    if (m_resourcePassEncoder.isOpen)
    {
        RHI_VALIDATION_ERROR(
            "A resource pass encoder on this command buffer is still open. "
            "IResourcePassEncoder::end() must be called before closing a command buffer."
        );
    }
    isOpen = false;
    baseObject->close();
}

Result DebugCommandBuffer::getNativeHandle(NativeHandle* outHandle)
{
    SLANG_RHI_API_FUNC;
    return baseObject->getNativeHandle(outHandle);
}

void DebugCommandBuffer::invalidateDescriptorHeapBinding()
{
    SLANG_RHI_API_FUNC;
    ComPtr<ICommandBufferD3D12> cmdBuf;
    if (SLANG_FAILED(baseObject->queryInterface(ICommandBufferD3D12::getTypeGuid(), (void**)cmdBuf.writeRef())))
    {
        RHI_VALIDATION_ERROR("The current command buffer implementation does not provide ICommandBufferD3D12 interface."
        );
        return;
    }
    return cmdBuf->invalidateDescriptorHeapBinding();
}

void DebugCommandBuffer::ensureInternalDescriptorHeapsBound()
{
    SLANG_RHI_API_FUNC;
    ComPtr<ICommandBufferD3D12> cmdBuf;
    if (SLANG_FAILED(baseObject->queryInterface(ICommandBufferD3D12::getTypeGuid(), (void**)cmdBuf.writeRef())))
    {
        RHI_VALIDATION_ERROR("The current command buffer implementation does not provide ICommandBufferD3D12 interface."
        );
        return;
    }
    return cmdBuf->ensureInternalDescriptorHeapsBound();
}

void DebugCommandBuffer::checkEncodersClosedBeforeNewEncoder()
{
    if (m_resourcePassEncoder.isOpen || m_renderPassEncoder.isOpen || m_computePassEncoder.isOpen ||
        m_rayTracingPassEncoder.isOpen)
    {
        RHI_VALIDATION_ERROR(
            "A previous pass encoder created on this command buffer is still open. "
            "end() must be called on the encoder before creating an encoder."
        );
    }
}

void DebugCommandBuffer::checkCommandBufferOpenWhenCreatingEncoder()
{
    if (!isOpen)
    {
        RHI_VALIDATION_ERROR(
            "The command buffer is already closed. Encoders can only be retrieved "
            "while the command buffer is open."
        );
    }
}

} // namespace rhi::debug
