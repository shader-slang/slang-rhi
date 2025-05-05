#include "d3d12-bindless-descriptor-set.h"
#include "d3d12-device.h"
#include "d3d12-buffer.h"
#include "d3d12-texture.h"
#include "d3d12-sampler.h"
#include "d3d12-acceleration-structure.h"

namespace rhi::d3d12 {

BindlessDescriptorSet::BindlessDescriptorSet(DeviceImpl* device, const BindlessDesc& desc)
    : m_device(device)
    , m_desc(desc)
{
}

BindlessDescriptorSet::~BindlessDescriptorSet()
{
    if (m_srvUavAllocation)
    {
        m_device->m_gpuCbvSrvUavHeap->free(m_srvUavAllocation);
    }
    if (m_samplerAllocation)
    {
        m_device->m_gpuSamplerHeap->free(m_samplerAllocation);
    }
}

Result BindlessDescriptorSet::initialize()
{
    m_srvUavAllocation = m_device->m_gpuCbvSrvUavHeap->allocate(
        m_desc.bufferCount + m_desc.textureCount + m_desc.accelerationStructureCount
    );
    if (!m_srvUavAllocation)
    {
        return SLANG_FAIL;
    }
    m_srvUavHeapOffset = m_srvUavAllocation.getHeapOffset();

    m_samplerAllocation = m_device->m_gpuSamplerHeap->allocate(m_desc.samplerCount);
    if (!m_samplerAllocation)
    {
        return SLANG_FAIL;
    }
    m_samplerHeapOffset = m_samplerAllocation.getHeapOffset();

    m_firstTextureHandle = m_desc.bufferCount;
    m_firstAccelerationStructureHandle = m_desc.bufferCount + m_desc.textureCount;

    m_bufferAllocator.capacity = m_desc.bufferCount;
    m_textureAllocator.capacity = m_desc.textureCount;
    m_samplerAllocator.capacity = m_desc.samplerCount;
    m_accelerationStructureAllocator.capacity = m_desc.accelerationStructureCount;

    return SLANG_OK;
}

Result BindlessDescriptorSet::allocBufferHandle(
    IBuffer* buffer,
    DescriptorHandleAccess access,
    Format format,
    BufferRange range,
    DescriptorHandle* outHandle
)
{
    uint32_t slot;
    SLANG_RETURN_ON_FAIL(m_bufferAllocator.allocate(&slot));

    BufferImpl* bufferImpl = checked_cast<BufferImpl*>(buffer);
    switch (access)
    {
    case DescriptorHandleAccess::Read:
        m_device->m_device->CopyDescriptorsSimple(
            1,
            m_srvUavAllocation.getCpuHandle(slot),
            bufferImpl->getSRV(format, 0, range),
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
        );
        outHandle->type = DescriptorHandleType::Buffer;
        break;
    case DescriptorHandleAccess::ReadWrite:
        m_device->m_device->CopyDescriptorsSimple(
            1,
            m_srvUavAllocation.getCpuHandle(slot),
            bufferImpl->getUAV(format, 0, range),
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
        );
        outHandle->type = DescriptorHandleType::RWBuffer;
        break;
    default:
        return SLANG_E_INVALID_ARG;
    }

    outHandle->value = m_srvUavHeapOffset + slot;

    return SLANG_OK;
}

Result BindlessDescriptorSet::allocTextureHandle(
    ITextureView* textureView,
    DescriptorHandleAccess access,
    DescriptorHandle* outHandle
)
{
    uint32_t slot;
    SLANG_RETURN_ON_FAIL(m_textureAllocator.allocate(&slot));

    TextureViewImpl* textureViewImpl = checked_cast<TextureViewImpl*>(textureView);
    switch (access)
    {
    case DescriptorHandleAccess::Read:
        m_device->m_device->CopyDescriptorsSimple(
            1,
            m_srvUavAllocation.getCpuHandle(m_firstTextureHandle + slot),
            textureViewImpl->getSRV(),
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
        );
        outHandle->type = DescriptorHandleType::Texture;
        break;
    case DescriptorHandleAccess::ReadWrite:
        m_device->m_device->CopyDescriptorsSimple(
            1,
            m_srvUavAllocation.getCpuHandle(m_firstTextureHandle + slot),
            textureViewImpl->getUAV(),
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
        );
        outHandle->type = DescriptorHandleType::RWTexture;
        break;
    default:
        return SLANG_E_INVALID_ARG;
    }

    outHandle->value = m_srvUavHeapOffset + m_firstTextureHandle + slot;

    return SLANG_OK;
}

Result BindlessDescriptorSet::allocSamplerHandle(ISampler* sampler, DescriptorHandle* outHandle)
{
    uint32_t slot;
    SLANG_RETURN_ON_FAIL(m_samplerAllocator.allocate(&slot));

    SamplerImpl* samplerImpl = checked_cast<SamplerImpl*>(sampler);
    m_device->m_device->CopyDescriptorsSimple(
        1,
        m_samplerAllocation.getCpuHandle(slot),
        samplerImpl->m_descriptor.cpuHandle,
        D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER
    );

    outHandle->type = DescriptorHandleType::Sampler;
    outHandle->value = m_samplerHeapOffset + slot;

    return SLANG_OK;
}

Result BindlessDescriptorSet::allocAccelerationStructureHandle(
    IAccelerationStructure* accelerationStructure,
    DescriptorHandle* outHandle
)
{
    uint32_t slot;
    SLANG_RETURN_ON_FAIL(m_accelerationStructureAllocator.allocate(&slot));

    AccelerationStructureImpl* asImpl = checked_cast<AccelerationStructureImpl*>(accelerationStructure);
    m_device->m_device->CopyDescriptorsSimple(
        1,
        m_srvUavAllocation.getCpuHandle(m_firstAccelerationStructureHandle + slot),
        asImpl->m_descriptor.cpuHandle,
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
    );

    outHandle->type = DescriptorHandleType::AccelerationStructure;
    outHandle->value = m_srvUavHeapOffset + m_firstAccelerationStructureHandle + slot;

    return SLANG_OK;
}

Result BindlessDescriptorSet::freeHandle(const DescriptorHandle& handle)
{
    switch (handle.type)
    {
    case DescriptorHandleType::Buffer:
    case DescriptorHandleType::RWBuffer:
        return m_bufferAllocator.free(handle.value - m_srvUavHeapOffset);
    case DescriptorHandleType::Texture:
    case DescriptorHandleType::RWTexture:
        return m_textureAllocator.free(handle.value - m_srvUavHeapOffset - m_firstTextureHandle);
    case DescriptorHandleType::Sampler:
        return m_samplerAllocator.free(handle.value - m_samplerHeapOffset);
    case DescriptorHandleType::AccelerationStructure:
        return m_accelerationStructureAllocator.free(
            handle.value - m_srvUavHeapOffset - m_firstAccelerationStructureHandle
        );
    default:
        return SLANG_E_INVALID_ARG;
    }
}

} // namespace rhi::d3d12
