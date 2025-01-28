#include "vk-sampler.h"
#include "vk-device.h"
#include "vk-util.h"

namespace rhi::vk {

SamplerImpl::SamplerImpl(DeviceImpl* device, const SamplerDesc& desc)
    : Sampler(desc)
    , m_device(device)
{
}

SamplerImpl::~SamplerImpl()
{
    m_device->m_api.vkDestroySampler(m_device->m_api.m_device, m_sampler, nullptr);
}

Result SamplerImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::VkSampler;
    outHandle->value = (uint64_t)m_sampler;
    return SLANG_OK;
}

Result DeviceImpl::createSampler(const SamplerDesc& desc, ISampler** outSampler)
{
    VkSamplerCreateInfo samplerInfo = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};

    samplerInfo.magFilter = VulkanUtil::translateFilterMode(desc.minFilter);
    samplerInfo.minFilter = VulkanUtil::translateFilterMode(desc.magFilter);

    samplerInfo.addressModeU = VulkanUtil::translateAddressingMode(desc.addressU);
    samplerInfo.addressModeV = VulkanUtil::translateAddressingMode(desc.addressV);
    samplerInfo.addressModeW = VulkanUtil::translateAddressingMode(desc.addressW);

    samplerInfo.anisotropyEnable = desc.maxAnisotropy > 1;
    samplerInfo.maxAnisotropy = (float)desc.maxAnisotropy;

    // TODO: support translation of border color...
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;

    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = desc.reductionOp == TextureReductionOp::Comparison;
    samplerInfo.compareOp = VulkanUtil::translateComparisonFunc(desc.comparisonFunc);
    samplerInfo.mipmapMode = VulkanUtil::translateMipFilterMode(desc.mipFilter);
    samplerInfo.minLod = max(0.0f, desc.minLOD);
    samplerInfo.maxLod = clamp(desc.maxLOD, samplerInfo.minLod, VK_LOD_CLAMP_NONE);

    VkSamplerReductionModeCreateInfo reductionInfo = {VK_STRUCTURE_TYPE_SAMPLER_REDUCTION_MODE_CREATE_INFO};
    reductionInfo.reductionMode = VulkanUtil::translateReductionOp(desc.reductionOp);
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
