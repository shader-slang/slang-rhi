#include "metal-sampler.h"
#include "metal-device.h"
#include "metal-utils.h"

namespace rhi::metal {

SamplerImpl::SamplerImpl(Device* device, const SamplerDesc& desc)
    : Sampler(device, desc)
{
}

SamplerImpl::~SamplerImpl() {}

Result SamplerImpl::init()
{
    NS::SharedPtr<MTL::SamplerDescriptor> samplerDesc = NS::TransferPtr(MTL::SamplerDescriptor::alloc()->init());

    samplerDesc->setMinFilter(translateSamplerMinMagFilter(m_desc.minFilter));
    samplerDesc->setMagFilter(translateSamplerMinMagFilter(m_desc.magFilter));
    samplerDesc->setMipFilter(translateSamplerMipFilter(m_desc.mipFilter));

    samplerDesc->setSAddressMode(translateSamplerAddressMode(m_desc.addressU));
    samplerDesc->setTAddressMode(translateSamplerAddressMode(m_desc.addressV));
    samplerDesc->setRAddressMode(translateSamplerAddressMode(m_desc.addressW));

    samplerDesc->setMaxAnisotropy(clamp(m_desc.maxAnisotropy, 1u, 16u));

    // Determine border color.
    // Check for predefined border colors.
    // If no match is found, use transparent black.
    {
        struct BorderColor
        {
            float color[4];
            MTL::SamplerBorderColor borderColor;
        };
        static const BorderColor borderColors[] = {
            {{0.0f, 0.0f, 0.0f, 0.0f}, MTL::SamplerBorderColorTransparentBlack},
            {{0.0f, 0.0f, 0.0f, 1.0f}, MTL::SamplerBorderColorOpaqueBlack},
            {{1.0f, 1.0f, 1.0f, 1.0f}, MTL::SamplerBorderColorOpaqueWhite},
        };
        const BorderColor* borderColor = nullptr;
        for (const auto& bc : borderColors)
        {
            if (::memcmp(bc.color, m_desc.borderColor, 4 * sizeof(float)) == 0)
            {
                borderColor = &bc;
                samplerDesc->setBorderColor(bc.borderColor);
                break;
            }
        }
        if (!borderColor)
        {
            samplerDesc->setBorderColor(MTL::SamplerBorderColorTransparentBlack);
        }
    }

    samplerDesc->setNormalizedCoordinates(true);

    samplerDesc->setCompareFunction(translateCompareFunction(m_desc.comparisonFunc));
    samplerDesc->setLodMinClamp(clamp(m_desc.minLOD, 0.f, 1000.f));
    samplerDesc->setLodMaxClamp(clamp(m_desc.maxLOD, samplerDesc->lodMinClamp(), 1000.f));

    samplerDesc->setSupportArgumentBuffers(true);
    if (m_desc.label)
    {
        samplerDesc->setLabel(createString(m_desc.label).get());
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
