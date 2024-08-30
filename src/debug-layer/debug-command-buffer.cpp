#include "debug-command-buffer.h"
#include "debug-framebuffer.h"
#include "debug-helper-functions.h"
#include "debug-render-pass.h"

namespace rhi::debug {

DebugCommandBuffer::DebugCommandBuffer()
{
    SLANG_RHI_API_FUNC;
    m_renderCommandEncoder.commandBuffer = this;
    m_computeCommandEncoder.commandBuffer = this;
    m_resourceCommandEncoder.commandBuffer = this;
    m_rayTracingCommandEncoder.commandBuffer = this;
}

ICommandBuffer* DebugCommandBuffer::getInterface(const Slang::Guid& guid)
{
    if (guid == GfxGUID::IID_ICommandBuffer || guid == GfxGUID::IID_ISlangUnknown)
        return (DebugObject<ICommandBuffer>*)this;
    if (guid == GfxGUID::IID_ICommandBufferD3D12)
        return static_cast<ICommandBufferD3D12*>(this);
    return nullptr;
}

void DebugCommandBuffer::encodeRenderCommands(
    IRenderPassLayout* renderPass,
    IFramebuffer* framebuffer,
    IRenderCommandEncoder** outEncoder
)
{
    SLANG_RHI_API_FUNC;
    checkCommandBufferOpenWhenCreatingEncoder();
    checkEncodersClosedBeforeNewEncoder();
    auto innerRenderPass = getInnerObj(renderPass);
    auto innerFramebuffer = getInnerObj(framebuffer);
    m_renderCommandEncoder.isOpen = true;
    baseObject->encodeRenderCommands(innerRenderPass, innerFramebuffer, &m_renderCommandEncoder.baseObject);
    if (m_renderCommandEncoder.baseObject)
        *outEncoder = &m_renderCommandEncoder;
    else
        *outEncoder = nullptr;
}

void DebugCommandBuffer::encodeComputeCommands(IComputeCommandEncoder** outEncoder)
{
    SLANG_RHI_API_FUNC;
    checkCommandBufferOpenWhenCreatingEncoder();
    checkEncodersClosedBeforeNewEncoder();
    m_computeCommandEncoder.isOpen = true;
    baseObject->encodeComputeCommands(&m_computeCommandEncoder.baseObject);
    if (m_computeCommandEncoder.baseObject)
    {
        *outEncoder = &m_computeCommandEncoder;
    }
    else
    {
        *outEncoder = nullptr;
    }
}

void DebugCommandBuffer::encodeResourceCommands(IResourceCommandEncoder** outEncoder)
{
    SLANG_RHI_API_FUNC;
    checkCommandBufferOpenWhenCreatingEncoder();
    checkEncodersClosedBeforeNewEncoder();
    m_resourceCommandEncoder.isOpen = true;
    baseObject->encodeResourceCommands(&m_resourceCommandEncoder.baseObject);
    if (m_resourceCommandEncoder.baseObject)
    {
        *outEncoder = &m_resourceCommandEncoder;
    }
    else
    {
        *outEncoder = nullptr;
    }
}

void DebugCommandBuffer::encodeRayTracingCommands(IRayTracingCommandEncoder** outEncoder)
{
    SLANG_RHI_API_FUNC;
    checkCommandBufferOpenWhenCreatingEncoder();
    checkEncodersClosedBeforeNewEncoder();
    m_rayTracingCommandEncoder.isOpen = true;
    baseObject->encodeRayTracingCommands(&m_rayTracingCommandEncoder.baseObject);
    if (m_rayTracingCommandEncoder.baseObject)
    {
        *outEncoder = &m_rayTracingCommandEncoder;
    }
    else
    {
        *outEncoder = nullptr;
    }
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
        RHI_VALIDATION_ERROR("A render command encoder on this command buffer is still open. "
                             "IRenderCommandEncoder::endEncoding() must be called before closing a command buffer.");
    }
    if (m_computeCommandEncoder.isOpen)
    {
        RHI_VALIDATION_ERROR("A compute command encoder on this command buffer is still open. "
                             "IComputeCommandEncoder::endEncoding() must be called before closing a command buffer.");
    }
    if (m_resourceCommandEncoder.isOpen)
    {
        RHI_VALIDATION_ERROR("A resource command encoder on this command buffer is still open. "
                             "IResourceCommandEncoder::endEncoding() must be called before closing a command buffer.");
    }
    isOpen = false;
    baseObject->close();
}

Result DebugCommandBuffer::getNativeHandle(InteropHandle* outHandle)
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
    if (m_renderCommandEncoder.isOpen || m_resourceCommandEncoder.isOpen || m_computeCommandEncoder.isOpen)
    {
        RHI_VALIDATION_ERROR("A previouse command encoder created on this command buffer is still open. "
                             "endEncoding() must be called on the encoder before creating an encoder.");
    }
}

void DebugCommandBuffer::checkCommandBufferOpenWhenCreatingEncoder()
{
    if (!isOpen)
    {
        RHI_VALIDATION_ERROR("The command buffer is already closed. Encoders can only be retrieved "
                             "while the command buffer is open.");
    }
}

} // namespace rhi::debug
