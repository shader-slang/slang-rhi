#include "d3d11-sampler.h"
#include "d3d11-device.h"
#include "d3d11-utils.h"

namespace rhi::d3d11 {

SamplerImpl::SamplerImpl(Device* device, const SamplerDesc& desc)
    : Sampler(device, desc)
{
}

Result DeviceImpl::createSampler(const SamplerDesc& desc, ISampler** outSampler)
{
    D3D11_FILTER_REDUCTION_TYPE dxReduction = translateFilterReduction(desc.reductionOp);
    D3D11_FILTER dxFilter;
    if (desc.maxAnisotropy > 1)
    {
        dxFilter = D3D11_ENCODE_ANISOTROPIC_FILTER(dxReduction);
    }
    else
    {
        D3D11_FILTER_TYPE dxMin = translateFilterMode(desc.minFilter);
        D3D11_FILTER_TYPE dxMag = translateFilterMode(desc.magFilter);
        D3D11_FILTER_TYPE dxMip = translateFilterMode(desc.mipFilter);

        dxFilter = D3D11_ENCODE_BASIC_FILTER(dxMin, dxMag, dxMip, dxReduction);
    }

    D3D11_SAMPLER_DESC dxDesc = {};
    dxDesc.Filter = dxFilter;
    dxDesc.AddressU = translateAddressingMode(desc.addressU);
    dxDesc.AddressV = translateAddressingMode(desc.addressV);
    dxDesc.AddressW = translateAddressingMode(desc.addressW);
    dxDesc.MipLODBias = desc.mipLODBias;
    dxDesc.MaxAnisotropy = desc.maxAnisotropy;
    dxDesc.ComparisonFunc = translateComparisonFunc(desc.comparisonFunc);
    for (int ii = 0; ii < 4; ++ii)
        dxDesc.BorderColor[ii] = desc.borderColor[ii];
    dxDesc.MinLOD = desc.minLOD;
    dxDesc.MaxLOD = desc.maxLOD;

    ComPtr<ID3D11SamplerState> sampler;
    SLANG_RETURN_ON_FAIL(m_device->CreateSamplerState(&dxDesc, sampler.writeRef()));

    RefPtr<SamplerImpl> samplerImpl = new SamplerImpl(this, desc);
    samplerImpl->m_sampler = sampler;
    returnComPtr(outSampler, samplerImpl);
    return SLANG_OK;
}

} // namespace rhi::d3d11
