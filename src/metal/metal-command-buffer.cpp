#include "metal-command-buffer.h"
#include "metal-command-encoder.h"
#include "metal-command-queue.h"
#include "metal-device.h"
#include "metal-shader-object.h"

namespace rhi::metal {

ICommandBuffer* CommandBufferImpl::getInterface(const Guid& guid)
{
    if (guid == GUID::IID_ISlangUnknown || guid == GUID::IID_ICommandBuffer)
        return static_cast<ICommandBuffer*>(this);
    return nullptr;
}

Result CommandBufferImpl::init(DeviceImpl* device, TransientResourceHeapImpl* transientHeap)
{
    m_device = device;
    m_commandBuffer = NS::RetainPtr(m_device->m_commandQueue->commandBuffer());
    return SLANG_OK;
}

Result CommandBufferImpl::beginResourcePass(IResourcePassEncoder** outEncoder)
{
    m_resourcePassEncoder.init(this);
    *outEncoder = &m_resourcePassEncoder return SLANG_OK;
}

Result CommandBufferImpl::beginRenderPass(const RenderPassDesc& desc, IRenderPassEncoder** outEncoder)
{
    m_renderPassEncoder.init(this);
    SLANG_RETURN_ON_FAIL(m_renderPassEncoder.beginPass(desc));
    *outEncoder = &m_renderPassEncoder;
    return SLANG_OK;
}

Result CommandBufferImpl::beginComputePass(IComputePassEncoder** outEncoder)
{
    m_computePassEncoder.init(this);
    *outEncoder = &m_computePassEncoder;
    return SLANG_OK;
}

Result CommandBufferImpl::beginRayTracingPass(IRayTracingPassEncoder** outEncoder)
{
    m_rayTracingPassEncoder.init(this);
    *outEncoder = &m_rayTracingPassEncoder;
    return SLANG_OK;
}

void CommandBufferImpl::close()
{
    // m_commandBuffer->commit();
}

Result CommandBufferImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::MTLCommandBuffer;
    outHandle->value = (uint64_t)m_commandBuffer.get();
    return SLANG_OK;
}

MTL::RenderCommandEncoder* CommandBufferImpl::getMetalRenderCommandEncoder(MTL::RenderPassDescriptor* renderPassDesc)
{
    if (!m_metalRenderCommandEncoder)
    {
        endMetalCommandEncoder();
        m_metalRenderCommandEncoder = NS::RetainPtr(m_commandBuffer->renderCommandEncoder(renderPassDesc));
    }
    return m_metalRenderCommandEncoder.get();
}

MTL::ComputeCommandEncoder* CommandBufferImpl::getMetalComputeCommandEncoder()
{
    if (!m_metalComputeCommandEncoder)
    {
        endMetalCommandEncoder();
        m_metalComputeCommandEncoder = NS::RetainPtr(m_commandBuffer->computeCommandEncoder());
    }
    return m_metalComputeCommandEncoder.get();
}

MTL::AccelerationStructureCommandEncoder* CommandBufferImpl::getMetalAccelerationStructureCommandEncoder()
{
    if (!m_metalAccelerationStructureCommandEncoder)
    {
        endMetalCommandEncoder();
        m_metalAccelerationStructureCommandEncoder =
            NS::RetainPtr(m_commandBuffer->accelerationStructureCommandEncoder());
    }
    return m_metalAccelerationStructureCommandEncoder.get();
}

MTL::BlitCommandEncoder* CommandBufferImpl::getMetalBlitCommandEncoder()
{
    if (!m_metalBlitCommandEncoder)
    {
        endMetalCommandEncoder();
        m_metalBlitCommandEncoder = NS::RetainPtr(m_commandBuffer->blitCommandEncoder());
    }
    return m_metalBlitCommandEncoder.get();
}

void CommandBufferImpl::endMetalCommandEncoder()
{
    if (m_metalRenderCommandEncoder)
    {
        m_metalRenderCommandEncoder->endEncoding();
        m_metalRenderCommandEncoder.reset();
    }
    if (m_metalComputeCommandEncoder)
    {
        m_metalComputeCommandEncoder->endEncoding();
        m_metalComputeCommandEncoder.reset();
    }
    if (m_metalAccelerationStructureCommandEncoder)
    {
        m_metalAccelerationStructureCommandEncoder->endEncoding();
        m_metalAccelerationStructureCommandEncoder.reset();
    }
    if (m_metalBlitCommandEncoder)
    {
        m_metalBlitCommandEncoder->endEncoding();
        m_metalBlitCommandEncoder.reset();
    }
}

} // namespace rhi::metal
