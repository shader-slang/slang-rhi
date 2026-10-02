#include "vk-descriptor-set-composition.h"
#include "vk-shader-object-layout.h"
#include "vk-device.h"

#include <algorithm>

namespace rhi::vk {

namespace {

using DescriptorSetInfo = ShaderObjectLayoutImpl::DescriptorSetInfo;

// Record each object's placement while collecting its descriptor sets. Consider
// two ParameterBlock<Params> fields, a and b: their layouts may be identical, but
// their sets differ. The subobject slot identifies the occurrence, so binding b
// does not depend on having visited a first. Constant buffers do not allocate
// sets here, but their children can contain parameter blocks and need placements.
Result composeChildren(
    DeviceImpl* device,
    const std::vector<ShaderObjectLayoutImpl::SubObjectRangeInfo>& subObjects,
    const std::vector<ShaderObjectLayoutImpl::BindingRangeInfo>& ranges,
    std::vector<DescriptorSetInfo>& sets,
    DescriptorSetPlacement& placement
)
{
    for (const auto& subObject : subObjects)
    {
        const auto& range = ranges[subObject.bindingRangeIndex];
        if (!subObject.layout || (range.bindingType != slang::BindingType::ParameterBlock &&
                                  range.bindingType != slang::BindingType::ConstantBuffer &&
                                  range.bindingType != slang::BindingType::PushConstant))
            continue;

        placement.subObjects.resize(std::max<size_t>(placement.subObjects.size(), range.subObjectIndex + range.count));
        for (uint32_t i = 0; i < range.count; ++i)
        {
            auto& child = placement.subObjects[range.subObjectIndex + i];
            if (range.bindingType == slang::BindingType::ParameterBlock)
            {
                child.firstSet = uint32_t(sets.size());
                if (sets.size() + subObject.layout->getOwnDescriptorSetCount() > kMaxDescriptorSets)
                {
                    device->printError("Composed descriptor set count exceeds Vulkan layout limit");
                    return SLANG_E_INVALID_ARG;
                }
                for (const auto& set : subObject.layout->getOwnDescriptorSets())
                {
                    // The composed root owns its Vulkan handles. Do not borrow the
                    // child layout's handle: added bindings may change this set.
                    DescriptorSetInfo info;
                    info.space = int32_t(sets.size());
                    info.vkBindings = set.vkBindings;
                    sets.push_back(std::move(info));
                }
            }
            SLANG_RETURN_ON_FAIL(composeChildren(
                device,
                subObject.layout->getSubObjectRanges(),
                subObject.layout->getBindingRanges(),
                sets,
                child
            ));
        }
    }
    return SLANG_OK;
}

} // namespace

Result RootShaderObjectLayoutImpl::Builder::findOrAddComposedDescriptorSet(
    uint32_t space,
    uint32_t* outDescriptorSetIndex
)
{
    if (space >= kMaxDescriptorSets)
    {
        m_device->handleMessage(
            DebugMessageType::Error,
            DebugMessageSource::Layer,
            "Descriptor set space exceeds Vulkan layout limit"
        );
        return SLANG_E_INVALID_ARG;
    }

    const uint32_t neededCount = space + 1;
    if (m_descriptorSetBuildInfos.size() < neededCount)
    {
        const uint32_t oldCount = (uint32_t)m_descriptorSetBuildInfos.size();
        m_descriptorSetBuildInfos.resize(neededCount);
        for (uint32_t i = oldCount; i < neededCount; ++i)
        {
            m_descriptorSetBuildInfos[i].space = (int32_t)i;
        }
    }

    *outDescriptorSetIndex = space;
    return SLANG_OK;
}

Result RootShaderObjectLayoutImpl::Builder::composeDescriptorSets()
{
    // Root descriptor spaces are absolute. Preserve holes before appending the
    // parameter-block sets, then let callers add bindings to the resulting sets.
    std::vector<DescriptorSetInfo> sets;
    for (const auto& set : m_descriptorSetBuildInfos)
    {
        if (set.space < 0 || uint32_t(set.space) >= kMaxDescriptorSets)
        {
            m_device->printError("Descriptor set space exceeds Vulkan layout limit");
            return SLANG_E_INVALID_ARG;
        }
        sets.resize(std::max<size_t>(sets.size(), size_t(set.space) + 1));
        sets[set.space].vkBindings = set.vkBindings;
    }
    for (uint32_t i = 0; i < sets.size(); ++i)
        sets[i].space = int32_t(i);

    auto composition = std::make_unique<DescriptorSetComposition>();
    SLANG_RETURN_ON_FAIL(composeChildren(m_device, m_subObjectRanges, m_bindingRanges, sets, composition->root));
    composition->entryPoints.resize(m_entryPoints.size());
    for (size_t i = 0; i < m_entryPoints.size(); ++i)
    {
        auto* layout = m_entryPoints[i].layout.get();
        SLANG_RETURN_ON_FAIL(composeChildren(
            m_device,
            layout->getSubObjectRanges(),
            layout->getBindingRanges(),
            sets,
            composition->entryPoints[i]
        ));
    }
    m_descriptorSetBuildInfos = std::move(sets);
    m_descriptorSetComposition = std::move(composition);
    m_childDescriptorSetCount = 0;
    return SLANG_OK;
}

} // namespace rhi::vk
