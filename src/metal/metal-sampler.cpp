#include "metal-sampler.h"
#include "metal-device.h"
#include "metal-util.h"

namespace rhi::metal {

SamplerImpl::SamplerImpl(const SamplerDesc& desc)
    : Sampler(desc)
{
}

SamplerImpl::~SamplerImpl() {}

Result SamplerImpl::init(DeviceImpl* device, const SamplerDesc& desc)
{
    NS::SharedPtr<MTL::SamplerDescriptor> samplerDesc = NS::TransferPtr(MTL::SamplerDescriptor::alloc()->init());

    samplerDesc->setMinFilter(MetalUtil::translateSamplerMinMagFilter(desc.minFilter));
    samplerDesc->setMagFilter(MetalUtil::translateSamplerMinMagFilter(desc.magFilter));
    samplerDesc->setMipFilter(MetalUtil::translateSamplerMipFilter(desc.mipFilter));

    samplerDesc->setSAddressMode(MetalUtil::translateSamplerAddressMode(desc.addressU));
    samplerDesc->setTAddressMode(MetalUtil::translateSamplerAddressMode(desc.addressV));
    samplerDesc->setRAddressMode(MetalUtil::translateSamplerAddressMode(desc.addressW));

    samplerDesc->setMaxAnisotropy(clamp(desc.maxAnisotropy, 1u, 16u));

    // TODO: support translation of border color...
    MTL::SamplerBorderColor borderColor = MTL::SamplerBorderColorOpaqueBlack;
    samplerDesc->setBorderColor(borderColor);

    samplerDesc->setNormalizedCoordinates(true);

    samplerDesc->setCompareFunction(MetalUtil::translateCompareFunction(desc.comparisonFunc));
    samplerDesc->setLodMinClamp(clamp(desc.minLOD, 0.f, 1000.f));
    samplerDesc->setLodMaxClamp(clamp(desc.maxLOD, samplerDesc->lodMinClamp(), 1000.f));

    samplerDesc->setSupportArgumentBuffers(true);
    if (desc.label)
    {
        samplerDesc->setLabel(MetalUtil::createString(desc.label).get());
    }

    // TODO: no support for reduction op

    m_samplerState = NS::TransferPtr(device->m_device->newSamplerState(samplerDesc.get()));

    return m_samplerState ? SLANG_OK : SLANG_FAIL;
}

Result SamplerImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::MTLSamplerState;
    outHandle->value = (uint64_t)(m_samplerState.get());
    return SLANG_OK;
}

Result DeviceImpl::createSampler(const SamplerDesc& desc, ISampler** outSampler)
{
    AUTORELEASEPOOL

    RefPtr<SamplerImpl> samplerImpl = new SamplerImpl(desc);
    SLANG_RETURN_ON_FAIL(samplerImpl->init(this, desc));
    returnComPtr(outSampler, samplerImpl);
    return SLANG_OK;
}

} // namespace rhi::metal
