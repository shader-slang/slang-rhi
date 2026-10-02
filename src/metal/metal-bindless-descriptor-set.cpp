#include "metal-bindless-descriptor-set.h"
#include "metal-device.h"
#include "metal-buffer.h"
#include "metal-texture.h"
#include "metal-sampler.h"
#include "metal-acceleration-structure.h"

namespace rhi::metal {

// The texture/sampler/AS handles store gpuResourceID()._impl and the arg-buffer consumer memcpy's
// the whole MTL::ResourceID; that equivalence holds only while ResourceID is a single uint64, so a
// metal-cpp bump that widened it must not silently pass here.
static_assert(sizeof(MTL::ResourceID) == sizeof(uint64_t), "MTL::ResourceID must be a single uint64");

BindlessDescriptorSet::BindlessDescriptorSet(DeviceImpl*, const BindlessDesc&) {}

Result BindlessDescriptorSet::initialize()
{
    // Nothing to allocate: handles are the resources' raw native ids (see header).
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
    // Metal bindless buffers are raw `device T*` pointers, so the format is irrelevant here
    // (unlike the typed-buffer-view backends) and only the access decides read vs read-write.
    SLANG_UNUSED(format);

    switch (access)
    {
    case DescriptorHandleAccess::Read:
        outHandle->type = DescriptorHandleType::Buffer;
        break;
    case DescriptorHandleAccess::ReadWrite:
        outHandle->type = DescriptorHandleType::RWBuffer;
        break;
    default:
        return SLANG_E_INVALID_ARG;
    }

    BufferImpl* bufferImpl = checked_cast<BufferImpl*>(buffer);
    range = bufferImpl->resolveBufferRange(range);
    outHandle->value = bufferImpl->getDeviceAddress() + range.offset;

    return SLANG_OK;
}

Result BindlessDescriptorSet::allocTextureHandle(
    ITextureView* textureView,
    DescriptorHandleAccess access,
    DescriptorHandle* outHandle
)
{
    switch (access)
    {
    case DescriptorHandleAccess::Read:
        outHandle->type = DescriptorHandleType::Texture;
        break;
    case DescriptorHandleAccess::ReadWrite:
        outHandle->type = DescriptorHandleType::RWTexture;
        break;
    default:
        return SLANG_E_INVALID_ARG;
    }

    TextureViewImpl* textureViewImpl = checked_cast<TextureViewImpl*>(textureView);
    outHandle->value = textureViewImpl->m_textureView->gpuResourceID()._impl;

    return SLANG_OK;
}

Result BindlessDescriptorSet::allocSamplerHandle(ISampler* sampler, DescriptorHandle* outHandle)
{
    SamplerImpl* samplerImpl = checked_cast<SamplerImpl*>(sampler);
    outHandle->type = DescriptorHandleType::Sampler;
    outHandle->value = samplerImpl->m_samplerState->gpuResourceID()._impl;
    return SLANG_OK;
}

Result BindlessDescriptorSet::allocAccelerationStructureHandle(
    IAccelerationStructure* accelerationStructure,
    DescriptorHandle* outHandle
)
{
    AccelerationStructureImpl* asImpl = checked_cast<AccelerationStructureImpl*>(accelerationStructure);
    outHandle->type = DescriptorHandleType::AccelerationStructure;
    outHandle->value = asImpl->m_accelerationStructure->gpuResourceID()._impl;
    return SLANG_OK;
}

} // namespace rhi::metal
