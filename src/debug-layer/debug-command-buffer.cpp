#include "debug-command-buffer.h"
#include "debug-framebuffer.h"
#include "debug-helper-functions.h"

namespace rhi::debug {

DebugCommandBuffer::DebugCommandBuffer()
{
    SLANG_RHI_API_FUNC;
    m_renderCommandEncoder.commandBuffer = this;
    m_computeCommandEncoder.commandBuffer = this;
    m_resourceCommandEncoder.commandBuffer = this;
    m_rayTracingCommandEncoder.commandBuffer = this;
}

ICommandBuffer* DebugCommandBuffer::getInterface(const Guid& guid)
{
    if (guid == GUID::IID_ICommandBuffer || guid == GUID::IID_ISlangUnknown)
        return (DebugObject<ICommandBuffer>*)this;
    if (guid == GUID::IID_ICommandBufferD3D12)
        return static_cast<ICommandBufferD3D12*>(this);
    return nullptr;
}

Result DebugCommandBuffer::encodeResourceCommands(IResourceCommandEncoder** outEncoder)
{
    SLANG_RHI_API_FUNC;
    checkCommandBufferOpenWhenCreatingEncoder();
    checkEncodersClosedBeforeNewEncoder();
    m_resourceCommandEncoder.isOpen = true;
    SLANG_RETURN_ON_FAIL(baseObject->encodeResourceCommands(&m_resourceCommandEncoder.baseObject));
    *outEncoder = &m_resourceCommandEncoder;
    return SLANG_OK;
}

Result DebugCommandBuffer::encodeRenderCommands(const RenderPassDesc& desc, IRenderCommandEncoder** outEncoder)
{
    SLANG_RHI_API_FUNC;
    checkCommandBufferOpenWhenCreatingEncoder();
    checkEncodersClosedBeforeNewEncoder();
    RenderPassDesc innerDesc = desc;
    for (Index i = 0; i < innerDesc.colorAttachmentCount; ++i)
    {
        innerDesc.colorAttachments[i].view = getInnerObj(desc.colorAttachments[i].view);
    }
    if (innerDesc.depthStencilAttachment.view)
    {
        innerDesc.depthStencilAttachment.view = getInnerObj(desc.depthStencilAttachment.view);
    }
    m_renderCommandEncoder.isOpen = true;
    SLANG_RETURN_ON_FAIL(baseObject->encodeRenderCommands(innerDesc, &m_renderCommandEncoder.baseObject));
    *outEncoder = &m_renderCommandEncoder;
    return SLANG_OK;
}

Result DebugCommandBuffer::encodeComputeCommands(IComputeCommandEncoder** outEncoder)
{
    SLANG_RHI_API_FUNC;
    checkCommandBufferOpenWhenCreatingEncoder();
    checkEncodersClosedBeforeNewEncoder();
    m_computeCommandEncoder.isOpen = true;
    SLANG_RETURN_ON_FAIL(baseObject->encodeComputeCommands(&m_computeCommandEncoder.baseObject));
    *outEncoder = &m_computeCommandEncoder;
    return SLANG_OK;
}

Result DebugCommandBuffer::encodeRayTracingCommands(IRayTracingCommandEncoder** outEncoder)
{
    SLANG_RHI_API_FUNC;
    checkCommandBufferOpenWhenCreatingEncoder();
    checkEncodersClosedBeforeNewEncoder();
    m_rayTracingCommandEncoder.isOpen = true;
    SLANG_RETURN_ON_FAIL(baseObject->encodeRayTracingCommands(&m_rayTracingCommandEncoder.baseObject));
    *outEncoder = &m_rayTracingCommandEncoder;
    return SLANG_OK;
}

void DebugCommandBuffer::close()
{
    SLANG_RHI_API_FUNC;
    if (!isOpen)
    {
        RHI_VALIDATION_ERROR("command buffer is already closed.");
    }
    if (m_renderCommandEncoder.isOpen)
    {
        RHI_VALIDATION_ERROR(
            "A render command encoder on this command buffer is still open. "
            "IRenderCommandEncoder::endEncoding() must be called before closing a command buffer."
        );
    }
    if (m_computeCommandEncoder.isOpen)
    {
        RHI_VALIDATION_ERROR(
            "A compute command encoder on this command buffer is still open. "
            "IComputeCommandEncoder::endEncoding() must be called before closing a command buffer."
        );
    }
    if (m_resourceCommandEncoder.isOpen)
    {
        RHI_VALIDATION_ERROR(
            "A resource command encoder on this command buffer is still open. "
            "IResourceCommandEncoder::endEncoding() must be called before closing a command buffer."
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
    if (m_resourceCommandEncoder.isOpen || m_renderCommandEncoder.isOpen || m_computeCommandEncoder.isOpen ||
        m_rayTracingCommandEncoder.isOpen)
    {
        RHI_VALIDATION_ERROR(
            "A previous command encoder created on this command buffer is still open. "
            "endEncoding() must be called on the encoder before creating an encoder."
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
