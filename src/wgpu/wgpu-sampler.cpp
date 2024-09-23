#include "wgpu-sampler.h"
#include "wgpu-device.h"
#include "wgpu-util.h"

namespace rhi::wgpu {

SamplerImpl::SamplerImpl(DeviceImpl* device, const SamplerDesc& desc)
    : Sampler(desc)
    , m_device(device)
{
}

SamplerImpl::~SamplerImpl()
{
    if (m_sampler)
    {
        m_device->m_ctx.api.wgpuSamplerRelease(m_sampler);
    }
}

Result SamplerImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::WGPUSampler;
    outHandle->value = (uint64_t)m_sampler;
    return SLANG_OK;
}

Result DeviceImpl::createSampler(SamplerDesc const& desc, ISampler** outSampler)
{
    RefPtr<SamplerImpl> sampler = new SamplerImpl(this, desc);
    WGPUSamplerDescriptor samplerDesc = {};
    samplerDesc.addressModeU = translateAddressMode(desc.addressU);
    samplerDesc.addressModeV = translateAddressMode(desc.addressV);
    samplerDesc.addressModeW = translateAddressMode(desc.addressW);
    samplerDesc.magFilter = translateFilterMode(desc.magFilter);
    samplerDesc.minFilter = translateFilterMode(desc.minFilter);
    samplerDesc.mipmapFilter = translateMipmapFilterMode(desc.mipFilter);
    samplerDesc.lodMinClamp = desc.minLOD;
    samplerDesc.lodMaxClamp = desc.maxLOD;
    samplerDesc.compare = translateCompareFunction(desc.comparisonFunc);
    samplerDesc.maxAnisotropy = desc.maxAnisotropy;
    samplerDesc.label = desc.label;
    sampler->m_sampler = m_ctx.api.wgpuDeviceCreateSampler(m_ctx.device, &samplerDesc);
    if (!sampler->m_sampler)
    {
        return SLANG_FAIL;
    }
    returnComPtr(outSampler, sampler);
    return SLANG_OK;
}

} // namespace rhi::wgpu
