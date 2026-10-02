// Adapts explicit extra-resource descriptors to ordinary Vulkan binding ranges.
// Descriptor-set composition owns object placement; runtime binding consumes the
// same resource slots and offsets as reflected bindings and has no synthetic IDs.
#include "vk-shader-object-layout.h"
#include "vk-device.h"
#include "synthetic-resource-bindings.h"

#include <limits>

namespace rhi::vk {

Result RootShaderObjectLayoutImpl::Builder::addSyntheticDescriptorSetBinding(
    uint32_t descriptorSetIndex,
    const VkDescriptorSetLayoutBinding& bindingDesc
)
{
    auto& descriptorSetInfo = m_descriptorSetBuildInfos[descriptorSetIndex];
    for (const auto& existingBinding : descriptorSetInfo.vkBindings)
    {
        if (existingBinding.binding == bindingDesc.binding)
        {
            m_device->handleMessage(
                DebugMessageType::Error,
                DebugMessageSource::Layer,
                "Duplicate Vulkan descriptor binding between reflected and synthetic resources"
            );
            return SLANG_E_INVALID_ARG;
        }
    }

    descriptorSetInfo.vkBindings.push_back(bindingDesc);
    return SLANG_OK;
}

Result RootShaderObjectLayoutImpl::Builder::addSyntheticResources()
{
    if (!m_syntheticResources)
        return SLANG_OK;

    // Collect reflected child sets before inserting synthetic resources or gaps.
    // Composition records each object occurrence's placement. Added bindings
    // can share these sets without moving reflected bindings or requiring the
    // runtime to repeat the layout builder's allocation order.
    SLANG_RETURN_ON_FAIL(composeDescriptorSets());

    for (const auto& resource : m_syntheticResources->getInputs())
    {
        SLANG_RETURN_ON_FAIL(_addSyntheticResource(resource));
    }

    return SLANG_OK;
}

Result RootShaderObjectLayoutImpl::Builder::_addSyntheticResource(const SyntheticResourceBindingRecord& resource)
{
    VkDescriptorType descriptorType = VK_DESCRIPTOR_TYPE_MAX_ENUM;
    SLANG_RETURN_ON_FAIL(_validateSyntheticResource(resource, &descriptorType));

    uint32_t bindingRangeIndex = 0;
    SLANG_RETURN_ON_FAIL(_addSyntheticDescriptorRange(resource, descriptorType, &bindingRangeIndex));
    _recordSyntheticBindingLocation(resource, bindingRangeIndex);
    return SLANG_OK;
}

Result RootShaderObjectLayoutImpl::Builder::_validateSyntheticResource(
    const SyntheticResourceBindingRecord& resource,
    VkDescriptorType* outDescriptorType
)
{
    if (!outDescriptorType)
        return SLANG_E_INVALID_ARG;
    if (resource.scope != SyntheticResourceScope::Global)
        return SLANG_E_NOT_IMPLEMENTED;
    if (resource.space < 0 || resource.binding < 0)
        return SLANG_E_INVALID_ARG;

    // Synthetic ranges have resource slots but no reflected subobject layout.
    // Accept only types that the runtime resource-slot binding loop can write.
    switch (resource.bindingType)
    {
    case slang::BindingType::Sampler:
    case slang::BindingType::CombinedTextureSampler:
    case slang::BindingType::Texture:
    case slang::BindingType::MutableTexture:
    case slang::BindingType::TypedBuffer:
    case slang::BindingType::MutableTypedBuffer:
    case slang::BindingType::RawBuffer:
    case slang::BindingType::MutableRawBuffer:
    case slang::BindingType::RayTracingAccelerationStructure:
        break;
    default:
        return SLANG_E_NOT_IMPLEMENTED;
    }

    VkDescriptorType descriptorType = _mapDescriptorType(resource.bindingType);
    if (descriptorType == VK_DESCRIPTOR_TYPE_MAX_ENUM)
        return SLANG_E_INVALID_ARG;

    if (resource.bindingType == slang::BindingType::RayTracingAccelerationStructure &&
        !m_device->m_api.m_extendedFeatures.accelerationStructureFeatures.accelerationStructure)
    {
        return SLANG_E_NOT_AVAILABLE;
    }

    *outDescriptorType = descriptorType;
    return SLANG_OK;
}

Result RootShaderObjectLayoutImpl::Builder::_addSyntheticDescriptorRange(
    const SyntheticResourceBindingRecord& resource,
    VkDescriptorType descriptorType,
    uint32_t* outBindingRangeIndex
)
{
    if (!outBindingRangeIndex)
        return SLANG_E_INVALID_ARG;

    uint32_t descriptorSetIndex = 0;
    SLANG_RETURN_ON_FAIL(findOrAddComposedDescriptorSet((uint32_t)resource.space, &descriptorSetIndex));

    VkDescriptorSetLayoutBinding vkBindingRangeDesc = {};
    vkBindingRangeDesc.binding = (uint32_t)resource.binding;
    vkBindingRangeDesc.descriptorCount = resource.arraySize;
    vkBindingRangeDesc.descriptorType = descriptorType;
    vkBindingRangeDesc.stageFlags = VK_SHADER_STAGE_ALL;
    SLANG_RETURN_ON_FAIL(addSyntheticDescriptorSetBinding(descriptorSetIndex, vkBindingRangeDesc));

    if (resource.arraySize > std::numeric_limits<uint32_t>::max() - m_slotCount)
        return SLANG_E_INVALID_ARG;

    uint32_t slotIndex = addResourceSlots(resource.bindingType, resource.arraySize);

    BindingRangeInfo bindingRangeInfo = {};
    bindingRangeInfo.bindingType = resource.bindingType;
    bindingRangeInfo.count = resource.arraySize;
    bindingRangeInfo.slotIndex = slotIndex;
    bindingRangeInfo.subObjectIndex = 0;
    bindingRangeInfo.isSpecializable = false;
    bindingRangeInfo.bindingOffset = (uint32_t)resource.binding;
    bindingRangeInfo.setOffset = (uint32_t)resource.space;

    *outBindingRangeIndex = (uint32_t)m_bindingRanges.size();
    m_bindingRanges.push_back(bindingRangeInfo);
    return SLANG_OK;
}

void RootShaderObjectLayoutImpl::Builder::_recordSyntheticBindingLocation(
    const SyntheticResourceBindingRecord& resource,
    uint32_t bindingRangeIndex
)
{
    SyntheticBindingLocation location = {};
    location.syntheticResourceID = resource.id;
    location.bindingType = resource.bindingType;
    location.arraySize = resource.arraySize;
    location.scope = resource.scope;
    location.entryPointIndex = resource.entryPointIndex;
    location.offset.bindingRangeIndex = bindingRangeIndex;
    location.debugName = resource.debugName.empty() ? nullptr : resource.debugName.c_str();

    m_syntheticLocations.push_back(location);
}

} // namespace rhi::vk
