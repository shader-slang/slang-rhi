#pragma once

#include "cuda-base.h"

namespace rhi::cuda {

class SamplerImpl : public Sampler
{
public:
    SamplerImpl(Device* device, const SamplerDesc& desc);
    ~SamplerImpl();
};

} // namespace rhi::cuda
