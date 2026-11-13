// Adapts explicit extra-resource descriptors to ordinary Vulkan binding ranges.
// Synthetic resources own separate sets. Runtime binding consumes the same
// resource slots and offsets as reflected bindings and has no synthetic IDs.
#include "vk-shader-object-layout.h"
#include "vk-device.h"
#include "synthetic-resource-bindings.h"

#include <limits>

namespace rhi::vk {

// Collect borrowed child layouts using the same relative offsets as runtime
// binding. No per-program placement tree or merged child layout is needed.
Result RootShaderObjectLayoutImpl::Builder::collectChildDescriptorSets(
    const std::vector<SubObjectRangeInfo>& subObjects,
    const std::vector<BindingRangeInfo>& ranges,
    uint64_t space
)
{
    for (const auto& subObject : subObjects)
    {
        const auto& range = ranges[subObject.bindingRangeIndex];
        if (!subObject.layout || (range.bindingType != slang::BindingType::ParameterBlock &&
                                  range.bindingType != slang::BindingType::ConstantBuffer &&
                                  range.bindingType != slang::BindingType::PushConstant))
            continue;
        for (uint32_t i = 0; i < range.count; ++i)
        {
            uint64_t childSpace = space + subObject.offset.space + i * subObject.stride.space;
            if (range.bindingType == slang::BindingType::ParameterBlock)
            {
                for (const auto& set : subObject.layout->getOwnDescriptorSets())
                {
                    uint64_t setIndex = childSpace + uint32_t(set.space);
                    if (set.space < 0 || setIndex >= kMaxDescriptorSets)
                    {
                        m_device->printError("Descriptor set space exceeds Vulkan layout limit");
                        return SLANG_E_INVALID_ARG;
                    }
                    if (m_descriptorSetLayouts.size() <= setIndex)
                        m_descriptorSetLayouts.resize(size_t(setIndex) + 1);
                    m_descriptorSetLayouts[size_t(setIndex)] = set.descriptorSetLayout;
                }
            }
            SLANG_RETURN_ON_FAIL(collectChildDescriptorSets(
                subObject.layout->getSubObjectRanges(),
                subObject.layout->getBindingRanges(),
                childSpace + subObject.elementSpace
            ));
        }
    }
    return SLANG_OK;
}

Result RootShaderObjectLayoutImpl::Builder::addSyntheticResources()
{
    if (!m_syntheticResources)
        return SLANG_OK;

    SLANG_RETURN_ON_FAIL(collectChildDescriptorSets(m_subObjectRanges, m_bindingRanges, m_globalSpace));
    for (const auto& entryPoint : m_entryPoints)
        SLANG_RETURN_ON_FAIL(collectChildDescriptorSets(
            entryPoint.layout->getSubObjectRanges(),
            entryPoint.layout->getBindingRanges(),
            entryPoint.offset.space
        ));

    // Validate all requested sets before adding any synthetic ranges. Several
    // synthetic resources may share a set, but none may extend a reflected set.
    for (const auto& resource : m_syntheticResources->getInputs())
    {
        if (resource.space < 0 || resource.space >= kMaxDescriptorSets)
        {
            m_device->handleMessage(
                DebugMessageType::Error,
                DebugMessageSource::Layer,
                "Descriptor set space exceeds Vulkan layout limit"
            );
            return SLANG_E_INVALID_ARG;
        }
        const auto& childLayouts = m_descriptorSetLayouts;
        bool reflectedSet = m_mapSpaceToDescriptorSetIndex.find(resource.space) != m_mapSpaceToDescriptorSetIndex.end();
        reflectedSet |=
            uint32_t(resource.space) < childLayouts.size() && childLayouts[resource.space] != VK_NULL_HANDLE;
        if (reflectedSet)
        {
            m_device->handleMessage(
                DebugMessageType::Error,
                DebugMessageSource::Layer,
                "Synthetic resources must use a separate Vulkan descriptor set. "
                "Choose a set not occupied by reflected resources and recompile the shader."
            );
            return SLANG_E_INVALID_ARG;
        }
    }

    for (const auto& resource : m_syntheticResources->getInputs())
    {
        SLANG_RETURN_ON_FAIL(_addSyntheticResource(resource));
    }

    // Root-owned empty sets fill gaps through the normal layout/allocation
    // path. Child-owned sets are left intact.
    for (const auto& set : m_descriptorSetBuildInfos)
    {
        if (set.space < 0 || uint32_t(set.space) >= kMaxDescriptorSets)
            return SLANG_E_INVALID_ARG;
        if (m_descriptorSetLayouts.size() <= uint32_t(set.space))
            m_descriptorSetLayouts.resize(uint32_t(set.space) + 1);
    }
    for (uint32_t space = 0; space < m_descriptorSetLayouts.size(); ++space)
    {
        uint32_t unused;
        if (!m_descriptorSetLayouts[space])
            SLANG_RETURN_ON_FAIL(findOrAddDescriptorSet(space, &unused));
    }

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

Result RootShaderObjectLayoutImpl::Builder::_addSyntheticResource(const SyntheticResourceBindingRecord& resource)
{
    VkDescriptorType descriptorType;
    SLANG_RETURN_ON_FAIL(_validateSyntheticResource(resource, &descriptorType));

    uint32_t descriptorSetIndex = 0;
    SLANG_RETURN_ON_FAIL(findOrAddDescriptorSet((uint32_t)resource.space, &descriptorSetIndex));
    auto& bindings = m_descriptorSetBuildInfos[descriptorSetIndex].vkBindings;
    for (const auto& existing : bindings)
    {
        if (existing.binding == uint32_t(resource.binding))
        {
            m_device->printError("Duplicate Vulkan descriptor binding between synthetic resources");
            return SLANG_E_INVALID_ARG;
        }
    }

    VkDescriptorSetLayoutBinding vkBindingRangeDesc = {};
    vkBindingRangeDesc.binding = (uint32_t)resource.binding;
    vkBindingRangeDesc.descriptorCount = resource.arraySize;
    vkBindingRangeDesc.descriptorType = descriptorType;
    vkBindingRangeDesc.stageFlags = VK_SHADER_STAGE_ALL;
    bindings.push_back(vkBindingRangeDesc);

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

    uint32_t bindingRangeIndex = (uint32_t)m_bindingRanges.size();
    m_bindingRanges.push_back(bindingRangeInfo);
    SyntheticBindingLocation location = {};
    location.syntheticResourceID = resource.id;
    location.bindingType = resource.bindingType;
    location.arraySize = resource.arraySize;
    location.scope = resource.scope;
    location.entryPointIndex = resource.entryPointIndex;
    location.offset.bindingRangeIndex = bindingRangeIndex;
    location.debugName = resource.debugName.empty() ? nullptr : resource.debugName.c_str();

    m_syntheticLocations.push_back(location);
    return SLANG_OK;
}

} // namespace rhi::vk
