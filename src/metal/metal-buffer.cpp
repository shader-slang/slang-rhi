#include "metal-buffer.h"
#include "metal-device.h"
#include "metal-utils.h"

namespace rhi::metal {

BufferImpl::BufferImpl(Device* device, const BufferDesc& desc)
    : Buffer(device, desc)
{
}

BufferImpl::~BufferImpl()
{
    if (m_deviceAddress != 0)
    {
        auto* device = getDevice<DeviceImpl>();
        if (device->m_residencySet)
        {
            device->m_residencySet->removeAllocation(m_buffer.get());
            device->m_residencySetDirty = true;
        }
    }
}

void BufferImpl::deleteThis()
{
    getDevice<DeviceImpl>()->deferDelete(this);
}

Result BufferImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::MTLBuffer;
    outHandle->value = (uint64_t)m_buffer.get();
    return SLANG_OK;
}

Result BufferImpl::getSharedHandle(NativeHandle* outHandle)
{
    *outHandle = {};
    return SLANG_E_NOT_AVAILABLE;
}

DeviceAddress BufferImpl::getDeviceAddress()
{
    return m_deviceAddress;
}

Result DeviceImpl::createBuffer(const BufferDesc& desc_, const void* initData, IBuffer** outBuffer)
{
    AUTORELEASEPOOL

    BufferDesc desc = fixupBufferDesc(desc_);

    const Size bufferSize = desc.size;

    MTL::ResourceOptions resourceOptions = MTL::ResourceOptions(0);
    switch (desc.memoryType)
    {
    case MemoryType::DeviceLocal:
        resourceOptions = MTL::ResourceStorageModePrivate;
        break;
    case MemoryType::Upload:
    case MemoryType::ReadBack:
        resourceOptions = MTL::ResourceStorageModeManaged;
        break;
    }

    RefPtr<BufferImpl> buffer(new BufferImpl(this, desc));
    buffer->m_buffer = NS::TransferPtr(m_device->newBuffer(bufferSize, resourceOptions));
    if (!buffer->m_buffer)
    {
        return SLANG_FAIL;
    }

    buffer->m_deviceAddress = buffer->m_buffer->gpuAddress();
    if (buffer->m_deviceAddress != 0)
    {
        if (m_residencySet)
        {
            m_residencySet->addAllocation(buffer->m_buffer.get());
            m_residencySetDirty = true;
        }
    }

    if (desc.label)
        buffer->m_buffer->addDebugMarker(createString(desc.label).get(), NS::Range(0, desc.size));

    if (initData)
    {
        NS::SharedPtr<MTL::Buffer> stagingBuffer =
            NS::TransferPtr(m_device->newBuffer(initData, bufferSize, MTL::ResourceStorageModeManaged));
        MTL::CommandBuffer* commandBuffer = m_commandQueue->commandBuffer();
        MTL::BlitCommandEncoder* encoder = commandBuffer->blitCommandEncoder();
        if (!stagingBuffer || !commandBuffer || !encoder)
        {
            return SLANG_FAIL;
        }
        encoder->copyFromBuffer(stagingBuffer.get(), 0, buffer->m_buffer.get(), 0, bufferSize);
        encoder->endEncoding();
        commandBuffer->commit();
        commandBuffer->waitUntilCompleted();
    }

    returnComPtr(outBuffer, buffer);
    return SLANG_OK;
}

Result DeviceImpl::createBufferFromNativeHandle(NativeHandle handle, const BufferDesc& desc, IBuffer** outBuffer)
{
    AUTORELEASEPOOL

    return SLANG_E_NOT_IMPLEMENTED;
}

Result DeviceImpl::mapBuffer(IBuffer* buffer, CpuAccessMode mode, void** outData)
{
    BufferImpl* bufferImpl = checked_cast<BufferImpl*>(buffer);
    bufferImpl->m_lastCpuAccessMode = mode;
    if (mode == CpuAccessMode::Read)
    {
        MTL::CommandBuffer* commandBuffer = m_commandQueue->commandBuffer();
        MTL::BlitCommandEncoder* encoder = commandBuffer->blitCommandEncoder();
        encoder->synchronizeResource(bufferImpl->m_buffer.get());
        encoder->endEncoding();
        commandBuffer->commit();
        commandBuffer->waitUntilCompleted();
    }
    *outData = bufferImpl->m_buffer->contents();
    return SLANG_OK;
}

Result DeviceImpl::unmapBuffer(IBuffer* buffer)
{
    BufferImpl* bufferImpl = checked_cast<BufferImpl*>(buffer);
    if (bufferImpl->m_lastCpuAccessMode == CpuAccessMode::Write)
    {
        bufferImpl->m_buffer->didModifyRange(NS::Range(0, bufferImpl->m_desc.size));
        MTL::CommandBuffer* commandBuffer = m_commandQueue->commandBuffer();
        MTL::BlitCommandEncoder* encoder = commandBuffer->blitCommandEncoder();
        encoder->synchronizeResource(bufferImpl->m_buffer.get());
        encoder->endEncoding();
        commandBuffer->commit();
        commandBuffer->waitUntilCompleted();
    }
    return SLANG_OK;
}

} // namespace rhi::metal
