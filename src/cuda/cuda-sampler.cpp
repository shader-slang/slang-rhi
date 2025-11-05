#include "cuda-sampler.h"
#include "cuda-device.h"

namespace rhi::cuda {

inline CUaddress_mode translateAddressMode(TextureAddressingMode mode)
{
    switch (mode)
    {
    case TextureAddressingMode::Wrap:
        return CU_TR_ADDRESS_MODE_WRAP;
    case TextureAddressingMode::ClampToEdge:
        return CU_TR_ADDRESS_MODE_CLAMP;
    case TextureAddressingMode::ClampToBorder:
        return CU_TR_ADDRESS_MODE_BORDER;
    case TextureAddressingMode::MirrorRepeat:
        return CU_TR_ADDRESS_MODE_MIRROR;
    case TextureAddressingMode::MirrorOnce:
        return CU_TR_ADDRESS_MODE_MIRROR;
    }
    return CU_TR_ADDRESS_MODE_WRAP;
}

inline CUfilter_mode translateFilterMode(TextureFilteringMode mode)
{
    switch (mode)
    {
    case TextureFilteringMode::Point:
        return CU_TR_FILTER_MODE_POINT;
    case TextureFilteringMode::Linear:
        return CU_TR_FILTER_MODE_LINEAR;
    }
    return CU_TR_FILTER_MODE_LINEAR;
}

SamplerImpl::SamplerImpl(Device* device, const SamplerDesc& desc)
    : Sampler(device, desc)
{
    m_samplerSettings.addressMode[0] = translateAddressMode(desc.addressU);
    m_samplerSettings.addressMode[1] = translateAddressMode(desc.addressV);
    m_samplerSettings.addressMode[2] = translateAddressMode(desc.addressW);
    m_samplerSettings.filterMode = translateFilterMode(desc.minFilter);
    m_samplerSettings.maxAnisotropy = desc.maxAnisotropy;
    m_samplerSettings.mipmapFilterMode = translateFilterMode(desc.mipFilter);
    m_samplerSettings.mipmapLevelBias = desc.mipLODBias;
    m_samplerSettings.minMipmapLevelClamp = desc.minLOD;
    m_samplerSettings.maxMipmapLevelClamp = desc.maxLOD;
    m_samplerSettings.borderColor[0] = desc.borderColor[0];
    m_samplerSettings.borderColor[1] = desc.borderColor[1];
    m_samplerSettings.borderColor[2] = desc.borderColor[2];
    m_samplerSettings.borderColor[3] = desc.borderColor[3];
}

SamplerImpl::~SamplerImpl() {}

} // namespace rhi::cuda
