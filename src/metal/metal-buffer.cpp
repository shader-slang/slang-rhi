#include "metal-buffer.h"
#include "metal-command.h"
#include "metal-device.h"
#include "metal-utils.h"

namespace rhi::metal {

BufferImpl::BufferImpl(Device* device, const BufferDesc& desc)
    : Buffer(device, desc)
{
}

BufferImpl::~BufferImpl()
{
    if (m_buffer)
    {
        getDevice<DeviceImpl>()->unregisterAllocation(m_buffer.get());
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
        resourceOptions = makeResourceOptions(MTL::ResourceStorageModePrivate);
        break;
    case MemoryType::Upload:
        resourceOptions = makeResourceOptions(MTL::ResourceStorageModeShared, MTL::ResourceCPUCacheModeWriteCombined);
        break;
    case MemoryType::ReadBack:
        resourceOptions = makeResourceOptions(MTL::ResourceStorageModeShared);
        break;
    default:
        SLANG_RHI_ASSERT_FAILURE("Unhandled MemoryType");
        return SLANG_FAIL;
    }

    RefPtr<BufferImpl> buffer(new BufferImpl(this, desc));
    buffer->m_buffer = NS::TransferPtr(m_device->newBuffer(bufferSize, resourceOptions));
    if (!buffer->m_buffer)
    {
        return SLANG_FAIL;
    }

    // GPU virtual address is stable immediately after allocation on Apple Silicon.
    // Residency set commit (in CommandQueueImpl::submit) happens before any
    // render/compute command buffer using this address via argument buffers.
    // Blit encoders handle residency for explicit operands automatically.
    buffer->m_deviceAddress = buffer->m_buffer->gpuAddress();
    registerAllocation(buffer->m_buffer.get());

    if (desc.label)
        buffer->m_buffer->addDebugMarker(createString(desc.label).get(), NS::Range(0, desc.size));

    if (initData)
    {
        if (desc.memoryType == MemoryType::DeviceLocal)
        {
            auto stagingOpts = makeResourceOptions(MTL::ResourceStorageModeShared);
            NS::SharedPtr<MTL::Buffer> stagingBuffer =
                NS::TransferPtr(m_device->newBuffer(initData, bufferSize, stagingOpts));
            if (!stagingBuffer)
                return SLANG_FAIL;
            MTL::CommandBuffer* commandBuffer = m_commandQueue->commandBuffer();
            if (!commandBuffer)
                return SLANG_FAIL;
            MTL::BlitCommandEncoder* encoder = commandBuffer->blitCommandEncoder();
            if (!encoder)
                return SLANG_FAIL;
            encoder->waitForFence(m_queue->m_queueFence.get());
            encoder->copyFromBuffer(stagingBuffer.get(), 0, buffer->m_buffer.get(), 0, bufferSize);
            encoder->updateFence(m_queue->m_queueFence.get());
            encoder->endEncoding();
            commandBuffer->commit();
            commandBuffer->waitUntilCompleted();
        }
        else
        {
            std::memcpy(buffer->m_buffer->contents(), initData, bufferSize);
        }
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
    SLANG_UNUSED(mode);
    BufferImpl* bufferImpl = checked_cast<BufferImpl*>(buffer);
    *outData = bufferImpl->m_buffer->contents();
    return SLANG_OK;
}

Result DeviceImpl::unmapBuffer(IBuffer* buffer)
{
    BufferImpl* bufferImpl = checked_cast<BufferImpl*>(buffer);
    (void)bufferImpl;
    return SLANG_OK;
}

} // namespace rhi::metal
