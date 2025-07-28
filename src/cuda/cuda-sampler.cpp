#include "cuda-sampler.h"
#include "cuda-device.h"

namespace rhi::cuda {

SamplerImpl::SamplerImpl(Device* device, const SamplerDesc& desc)
    : Sampler(device, desc)
{
}

SamplerImpl::~SamplerImpl()
{
}

} // namespace rhi::cuda
