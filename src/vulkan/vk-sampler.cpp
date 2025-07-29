#include "vk-sampler.h"
#include "vk-device.h"
#include "vk-utils.h"

namespace rhi::vk {

SamplerImpl::SamplerImpl(Device* device, const SamplerDesc& desc)
    : Sampler(device, desc)
{
}

SamplerImpl::~SamplerImpl()
{
    DeviceImpl* device = getDevice<DeviceImpl>();

    device->m_api.vkDestroySampler(device->m_api.m_device, m_sampler, nullptr);
}

Result SamplerImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::VkSampler;
    outHandle->value = (uint64_t)m_sampler;
    return SLANG_OK;
}

Result SamplerImpl::getDescriptorHandle(DescriptorHandle* outHandle)
{
    DeviceImpl* device = getDevice<DeviceImpl>();
    if (!device->m_bindlessDescriptorSet)
    {
        return SLANG_E_NOT_AVAILABLE;
    }
    if (!m_descriptorHandle)
    {
        SLANG_RETURN_FALSE_ON_FAIL(device->m_bindlessDescriptorSet->allocSamplerHandle(this, &m_descriptorHandle));
    }
    *outHandle = m_descriptorHandle;
    return SLANG_OK;
}

Result DeviceImpl::createSampler(const SamplerDesc& desc, ISampler** outSampler)
{
    VkSamplerCreateInfo samplerInfo = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};

    samplerInfo.magFilter = translateFilterMode(desc.magFilter);
    samplerInfo.minFilter = translateFilterMode(desc.minFilter);

    samplerInfo.addressModeU = translateAddressingMode(desc.addressU);
    samplerInfo.addressModeV = translateAddressingMode(desc.addressV);
    samplerInfo.addressModeW = translateAddressingMode(desc.addressW);

    samplerInfo.anisotropyEnable = desc.maxAnisotropy > 1;
    samplerInfo.maxAnisotropy = (float)desc.maxAnisotropy;

    VkSamplerCustomBorderColorCreateInfoEXT customBorderColorInfo = {
        VK_STRUCTURE_TYPE_SAMPLER_CUSTOM_BORDER_COLOR_CREATE_INFO_EXT
    };

    // Determine border color.
    // First, we check for predefined border colors.
    // If no match is found, we use custom border color if supported.
    // If custom border color is not supported, we use transparent black.
    {
        struct BorderColor
        {
            float color[4];
            VkBorderColor borderColor;
        };
        static const BorderColor borderColors[] = {
            {{0.0f, 0.0f, 0.0f, 0.0f}, VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK},
            {{0.0f, 0.0f, 0.0f, 1.0f}, VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK},
            {{1.0f, 1.0f, 1.0f, 1.0f}, VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE},
        };
        const BorderColor* borderColor = nullptr;
        for (const auto& bc : borderColors)
        {
            if (::memcmp(bc.color, desc.borderColor, 4 * sizeof(float)) == 0)
            {
                borderColor = &bc;
                samplerInfo.borderColor = bc.borderColor;
                break;
            }
        }
        if (!borderColor)
        {
            if (m_api.m_extendedFeatures.customBorderColorFeatures.customBorderColors)
            {
                samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_CUSTOM_EXT;
                customBorderColorInfo.customBorderColor.float32[0] = desc.borderColor[0];
                customBorderColorInfo.customBorderColor.float32[1] = desc.borderColor[1];
                customBorderColorInfo.customBorderColor.float32[2] = desc.borderColor[2];
                customBorderColorInfo.customBorderColor.float32[3] = desc.borderColor[3];
                customBorderColorInfo.pNext = samplerInfo.pNext;
                samplerInfo.pNext = &customBorderColorInfo;
            }
            else
            {
                samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
            }
        }
    }

    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = desc.reductionOp == TextureReductionOp::Comparison;
    samplerInfo.compareOp = translateComparisonFunc(desc.comparisonFunc);
    samplerInfo.mipmapMode = translateMipFilterMode(desc.mipFilter);
    samplerInfo.minLod = max(0.0f, desc.minLOD);
    samplerInfo.maxLod = clamp(desc.maxLOD, samplerInfo.minLod, VK_LOD_CLAMP_NONE);

    VkSamplerReductionModeCreateInfo reductionInfo = {VK_STRUCTURE_TYPE_SAMPLER_REDUCTION_MODE_CREATE_INFO};
    reductionInfo.reductionMode = translateReductionOp(desc.reductionOp);
    reductionInfo.pNext = samplerInfo.pNext;
    samplerInfo.pNext = &reductionInfo;

    VkSampler sampler;
    SLANG_VK_RETURN_ON_FAIL(m_api.vkCreateSampler(m_device, &samplerInfo, nullptr, &sampler));

    _labelObject((uint64_t)sampler, VK_OBJECT_TYPE_SAMPLER, desc.label);

    RefPtr<SamplerImpl> samplerImpl = new SamplerImpl(this, desc);
    samplerImpl->m_sampler = sampler;
    returnComPtr(outSampler, samplerImpl);
    return SLANG_OK;
}

} // namespace rhi::vk
