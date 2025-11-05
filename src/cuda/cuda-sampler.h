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
        return std::memcmp(this, &other, sizeof(SamplerSettings)) == 0;
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
