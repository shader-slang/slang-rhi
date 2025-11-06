#pragma once

#include "cuda-base.h"

namespace rhi::cuda {

struct SamplerSettings
{
    CUaddress_mode addressMode[3];
    CUfilter_mode filterMode;
    unsigned int maxAnisotropy;
    CUfilter_mode mipmapFilterMode;
    float mipmapLevelBias;
    float minMipmapLevelClamp;
    float maxMipmapLevelClamp;
    float borderColor[4];
    bool operator==(const SamplerSettings& other) const
    {
        return addressMode[0] == other.addressMode[0] && addressMode[1] == other.addressMode[1] &&
               addressMode[2] == other.addressMode[2] && filterMode == other.filterMode &&
               maxAnisotropy == other.maxAnisotropy && mipmapFilterMode == other.mipmapFilterMode &&
               mipmapLevelBias == other.mipmapLevelBias && minMipmapLevelClamp == other.minMipmapLevelClamp &&
               maxMipmapLevelClamp == other.maxMipmapLevelClamp && borderColor[0] == other.borderColor[0] &&
               borderColor[1] == other.borderColor[1] && borderColor[2] == other.borderColor[2] &&
               borderColor[3] == other.borderColor[3];
    }
};

class SamplerImpl : public Sampler
{
public:
    SamplerImpl(Device* device, const SamplerDesc& desc);
    ~SamplerImpl();

public:
    SamplerSettings m_samplerSettings;
};

} // namespace rhi::cuda
