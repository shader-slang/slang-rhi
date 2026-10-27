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
        auto* device = getDevice<DeviceImpl>();
        for (auto& [key, view] : m_textureViews)
            device->unregisterResource(view.get());
        device->unregisterResource(m_buffer.get());
    }
}

void BufferImpl::deleteThis()
{
    auto* device = getDevice<DeviceImpl>();
    // Stop resolving this raw address to an object whose public reference
    // count has reached zero, even though Metal destruction is deferred.
    if (!device->m_hasResidencySet && m_deviceAddress != 0)
        device->m_addressToBuffer.erase(m_deviceAddress, this);
    device->deferDelete(this);
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

Result BufferImpl::getDescriptorHandle(
    DescriptorHandleAccess access,
    Format format,
    BufferRange range,
    DescriptorHandle* outHandle
)
{
    *outHandle = {};
    if (!getDevice<DeviceImpl>()->m_hasResidencySet)
        return SLANG_E_NOT_AVAILABLE;
    if (access != DescriptorHandleAccess::Read && access != DescriptorHandleAccess::ReadWrite)
        return SLANG_E_INVALID_ARG;

    range = resolveBufferRange(range);
    if (range.size == 0)
        return SLANG_E_INVALID_ARG;

    uint64_t value;
    if (format == Format::Undefined)
        value = getDeviceAddress() + range.offset;
    else
    {
        MTL::Texture* view;
        SLANG_RETURN_ON_FAIL(getTextureView(format, range, access, &view));
        static_assert(sizeof(MTL::ResourceID) == sizeof(uint64_t));
        value = view->gpuResourceID()._impl;
    }
    outHandle->type =
        access == DescriptorHandleAccess::Read ? DescriptorHandleType::Buffer : DescriptorHandleType::RWBuffer;
    outHandle->value = value;
    return SLANG_OK;
}

Result BufferImpl::getTextureView(
    Format format,
    BufferRange range,
    DescriptorHandleAccess access,
    MTL::Texture** outView
)
{
    AUTORELEASEPOOL
    *outView = nullptr;
    if (access != DescriptorHandleAccess::Read && access != DescriptorHandleAccess::ReadWrite)
        return SLANG_E_INVALID_ARG;
    if (format == Format::Undefined || uint32_t(format) >= uint32_t(Format::_Count))
        return SLANG_E_INVALID_ARG;

    // Metal's texture-buffer format table is narrower than its ordinary texture format table.
    MTL::PixelFormat pixelFormat = translatePixelFormat(format);
    bool supportsReadWrite = false;
    switch (pixelFormat)
    {
    case MTL::PixelFormatR8Unorm:
    case MTL::PixelFormatR8Uint:
    case MTL::PixelFormatR8Sint:
    case MTL::PixelFormatR16Uint:
    case MTL::PixelFormatR16Sint:
    case MTL::PixelFormatR16Float:
    case MTL::PixelFormatR32Uint:
    case MTL::PixelFormatR32Sint:
    case MTL::PixelFormatR32Float:
    case MTL::PixelFormatRGBA8Unorm:
    case MTL::PixelFormatRGBA8Uint:
    case MTL::PixelFormatRGBA8Sint:
    case MTL::PixelFormatRGBA16Uint:
    case MTL::PixelFormatRGBA16Sint:
    case MTL::PixelFormatRGBA16Float:
    case MTL::PixelFormatRGBA32Uint:
    case MTL::PixelFormatRGBA32Sint:
    case MTL::PixelFormatRGBA32Float:
        supportsReadWrite = true;
        break;
    case MTL::PixelFormatR8Snorm:
    case MTL::PixelFormatR16Unorm:
    case MTL::PixelFormatR16Snorm:
    case MTL::PixelFormatRG8Unorm:
    case MTL::PixelFormatRG8Snorm:
    case MTL::PixelFormatRG8Uint:
    case MTL::PixelFormatRG8Sint:
    case MTL::PixelFormatRG16Unorm:
    case MTL::PixelFormatRG16Snorm:
    case MTL::PixelFormatRG16Uint:
    case MTL::PixelFormatRG16Sint:
    case MTL::PixelFormatRG16Float:
    case MTL::PixelFormatRGBA8Snorm:
    case MTL::PixelFormatBGRA8Unorm:
    case MTL::PixelFormatRGB10A2Unorm:
    case MTL::PixelFormatRGB10A2Uint:
    case MTL::PixelFormatRG11B10Float:
    case MTL::PixelFormatRG32Uint:
    case MTL::PixelFormatRG32Sint:
    case MTL::PixelFormatRG32Float:
    case MTL::PixelFormatRGBA16Unorm:
    case MTL::PixelFormatRGBA16Snorm:
        break;
    default:
        return SLANG_E_NOT_AVAILABLE;
    }

    auto* device = getDevice<DeviceImpl>();
    if (access == DescriptorHandleAccess::ReadWrite)
    {
        auto tier = device->m_device->readWriteTextureSupport();
        bool isR32 = pixelFormat == MTL::PixelFormatR32Uint || pixelFormat == MTL::PixelFormatR32Sint ||
                     pixelFormat == MTL::PixelFormatR32Float;
        if (!supportsReadWrite || tier == MTL::ReadWriteTextureTierNone ||
            (tier == MTL::ReadWriteTextureTier1 && !isR32))
            return SLANG_E_NOT_AVAILABLE;
    }

    range = resolveBufferRange(range);
    const Size elementSize = getFormatInfo(format).blockSizeInBytes;
    const Size alignment = device->m_device->minimumTextureBufferAlignmentForPixelFormat(pixelFormat);
    if (range.size == 0 || range.size % elementSize != 0 || range.offset % elementSize != 0 || alignment == 0 ||
        range.offset % alignment != 0 || range.size / elementSize > 256 * 1024 * 1024)
        return SLANG_E_INVALID_ARG;

    std::lock_guard<std::mutex> lock(m_textureViewsMutex);
    TextureViewKey key = {format, range.offset, range.size, access};
    auto it = m_textureViews.find(key);
    if (it != m_textureViews.end())
    {
        *outView = it->second.get();
        return SLANG_OK;
    }

    MTL::TextureUsage usage = MTL::TextureUsageShaderRead;
    if (access == DescriptorHandleAccess::ReadWrite)
        usage |= MTL::TextureUsageShaderWrite;
    auto desc = MTL::TextureDescriptor::textureBufferDescriptor(
        pixelFormat,
        range.size / elementSize,
        m_buffer->resourceOptions(),
        usage
    );
    auto view = NS::TransferPtr(m_buffer->newTexture(desc, range.offset, range.size));
    if (!view)
        return SLANG_FAIL;

    // Register the view as well as its backing buffer so shader validation can
    // resolve its resource ID when it is accessed indirectly.
    device->registerResource(view.get());
    *outView = view.get();
    m_textureViews.emplace(key, std::move(view));
    return SLANG_OK;
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
    if (!m_hasResidencySet && buffer->m_deviceAddress != 0)
        m_addressToBuffer.insert(buffer->m_deviceAddress, buffer->m_buffer->length(), buffer.get());
    registerResource(buffer->m_buffer.get());

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

    if (handle.type != NativeHandleType::MTLBuffer || handle.value == 0)
    {
        *outBuffer = nullptr;
        return SLANG_E_INVALID_HANDLE;
    }

    MTL::Buffer* nativeBuffer = reinterpret_cast<MTL::Buffer*>(handle.value);
    if (nativeBuffer->device() != m_device.get() || desc.size > nativeBuffer->length())
    {
        return SLANG_E_INVALID_ARG;
    }

    BufferDesc fixedDesc = fixupBufferDesc(desc);
    RefPtr<BufferImpl> buffer(new BufferImpl(this, fixedDesc));
    buffer->m_buffer = NS::RetainPtr(nativeBuffer);
    buffer->m_deviceAddress = nativeBuffer->gpuAddress();

    if (!m_hasResidencySet && buffer->m_deviceAddress != 0)
        m_addressToBuffer.insert(buffer->m_deviceAddress, nativeBuffer->length(), buffer.get());
    registerResource(nativeBuffer);

    if (fixedDesc.label)
        nativeBuffer->addDebugMarker(createString(fixedDesc.label).get(), NS::Range(0, fixedDesc.size));

    returnComPtr(outBuffer, buffer);
    return SLANG_OK;
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
    SLANG_UNUSED(bufferImpl);
    return SLANG_OK;
}

} // namespace rhi::metal
