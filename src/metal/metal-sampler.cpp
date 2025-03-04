#include "metal-sampler.h"
#include "metal-device.h"
#include "metal-util.h"

namespace rhi::metal {

SamplerImpl::SamplerImpl(Device* device, const SamplerDesc& desc)
    : Sampler(device, desc)
{
}

SamplerImpl::~SamplerImpl() {}

Result SamplerImpl::init()
{
    NS::SharedPtr<MTL::SamplerDescriptor> samplerDesc = NS::TransferPtr(MTL::SamplerDescriptor::alloc()->init());

    samplerDesc->setMinFilter(MetalUtil::translateSamplerMinMagFilter(m_desc.minFilter));
    samplerDesc->setMagFilter(MetalUtil::translateSamplerMinMagFilter(m_desc.magFilter));
    samplerDesc->setMipFilter(MetalUtil::translateSamplerMipFilter(m_desc.mipFilter));

    samplerDesc->setSAddressMode(MetalUtil::translateSamplerAddressMode(m_desc.addressU));
    samplerDesc->setTAddressMode(MetalUtil::translateSamplerAddressMode(m_desc.addressV));
    samplerDesc->setRAddressMode(MetalUtil::translateSamplerAddressMode(m_desc.addressW));

    samplerDesc->setMaxAnisotropy(clamp(m_desc.maxAnisotropy, 1u, 16u));

    // TODO: support translation of border color...
    MTL::SamplerBorderColor borderColor = MTL::SamplerBorderColorOpaqueBlack;
    samplerDesc->setBorderColor(borderColor);

    samplerDesc->setNormalizedCoordinates(true);

    samplerDesc->setCompareFunction(MetalUtil::translateCompareFunction(m_desc.comparisonFunc));
    samplerDesc->setLodMinClamp(clamp(m_desc.minLOD, 0.f, 1000.f));
    samplerDesc->setLodMaxClamp(clamp(m_desc.maxLOD, samplerDesc->lodMinClamp(), 1000.f));

    samplerDesc->setSupportArgumentBuffers(true);
    if (m_desc.label)
    {
        samplerDesc->setLabel(MetalUtil::createString(m_desc.label).get());
    }

    // TODO: no support for reduction op

    m_samplerState = NS::TransferPtr(getDevice<DeviceImpl>()->m_device->newSamplerState(samplerDesc.get()));

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

    RefPtr<SamplerImpl> samplerImpl = new SamplerImpl(this, desc);
    SLANG_RETURN_ON_FAIL(samplerImpl->init());
    returnComPtr(outSampler, samplerImpl);
    return SLANG_OK;
}

} // namespace rhi::metal
