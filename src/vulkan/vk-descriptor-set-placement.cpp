#include "vk-shader-object-layout.h"
#include "vk-device.h"

#include <algorithm>

namespace rhi::vk {

namespace {

// Return the register-space base of the unwrapped global or entry-point value.
// A compiler-generated constant-buffer wrapper can reserve a set before its
// element's parameter blocks, so both variable and element offsets contribute.
uint64_t getElementSpace(slang::VariableLayoutReflection* variable, slang::TypeLayoutReflection* elementType)
{
    uint64_t space = variable->getOffset(SLANG_PARAMETER_CATEGORY_SUB_ELEMENT_REGISTER_SPACE);
    auto* type = variable->getTypeLayout();
    if (type != elementType)
        space += type->getElementVarLayout()->getOffset(SLANG_PARAMETER_CATEGORY_SUB_ELEMENT_REGISTER_SPACE);
    return space;
}

// Record each object's placement while collecting its descriptor sets. Consider
// two ParameterBlock<Params> fields, a and b: their layouts may be identical, but
// their sets differ. The subobject slot identifies the occurrence, so binding b
// does not depend on having visited a first. Constant buffers do not allocate
// sets here, but their children can contain parameter blocks and need placements.
// The builder retains Slang's subobject-range order, so each range's reflected
// offset applies to the corresponding RHI range. Child element offsets account
// for sets reserved by the container before its nested parameter blocks.
Result placeChildren(
    DeviceImpl* device,
    slang::TypeLayoutReflection* typeLayout,
    uint64_t firstSpace,
    const std::vector<ShaderObjectLayoutImpl::SubObjectRangeInfo>& subObjects,
    const std::vector<ShaderObjectLayoutImpl::BindingRangeInfo>& ranges,
    std::vector<VkDescriptorSetLayout>& sets,
    DescriptorSetPlacement& placement
)
{
    for (size_t r = 0; r < subObjects.size(); ++r)
    {
        const auto& subObject = subObjects[r];
        const auto& range = ranges[subObject.bindingRangeIndex];
        if (!subObject.layout || (range.bindingType != slang::BindingType::ParameterBlock &&
                                  range.bindingType != slang::BindingType::ConstantBuffer &&
                                  range.bindingType != slang::BindingType::PushConstant))
            continue;

        placement.subObjects.resize(std::max<size_t>(placement.subObjects.size(), range.subObjectIndex + range.count));
        auto* offset = typeLayout->getSubObjectRangeOffset(SlangInt(r));
        auto* leafType = typeLayout->getBindingRangeLeafTypeLayout(subObject.bindingRangeIndex);
        auto* element = leafType->getElementVarLayout();
        // Register-space offsets describe whole child sets; descriptor-table
        // offsets describe bindings within a set and cannot substitute for them.
        const uint64_t rangeSpace = firstSpace + offset->getOffset(SLANG_PARAMETER_CATEGORY_SUB_ELEMENT_REGISTER_SPACE);
        const uint64_t spaceStride = leafType->getSize(SLANG_PARAMETER_CATEGORY_SUB_ELEMENT_REGISTER_SPACE);
        for (uint32_t i = 0; i < range.count; ++i)
        {
            auto& child = placement.subObjects[range.subObjectIndex + i];
            const uint64_t childSpace = rangeSpace + i * spaceStride;
            if (range.bindingType == slang::BindingType::ParameterBlock)
            {
                if (childSpace >= kMaxDescriptorSets)
                {
                    device->printError("Descriptor set space exceeds Vulkan layout limit");
                    return SLANG_E_INVALID_ARG;
                }
                child.firstSet = uint32_t(childSpace);
                for (const auto& set : subObject.layout->getOwnDescriptorSets())
                {
                    const uint64_t space = childSpace + uint32_t(set.space);
                    if (set.space < 0 || space >= kMaxDescriptorSets)
                    {
                        device->printError("Descriptor set space exceeds Vulkan layout limit");
                        return SLANG_E_INVALID_ARG;
                    }
                    // Keep the child's native layout intact. The root only
                    // borrows its handle for the pipeline layout.
                    sets.resize(std::max<size_t>(sets.size(), size_t(space) + 1));
                    sets[size_t(space)] = set.descriptorSetLayout;
                }
            }
            SLANG_RETURN_ON_FAIL(placeChildren(
                device,
                subObject.layout->getElementTypeLayout(),
                childSpace + element->getOffset(SLANG_PARAMETER_CATEGORY_SUB_ELEMENT_REGISTER_SPACE),
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

Result RootShaderObjectLayoutImpl::Builder::collectDescriptorSetPlacements()
{
    // Consider a ParameterBlock at set 0 and a root buffer explicitly at set 3.
    // Expanding the root to four sets does not move the child to set 4: Slang's
    // sub-element register-space offsets still place it at set 0. Place child
    // sets at those reflected locations, including inside root-set gaps.
    auto placements = std::make_unique<DescriptorSetPlacements>();
    auto& sets = placements->layouts;
    for (const auto& set : m_descriptorSetBuildInfos)
    {
        if (set.space < 0 || uint32_t(set.space) >= kMaxDescriptorSets)
        {
            m_device->printError("Descriptor set space exceeds Vulkan layout limit");
            return SLANG_E_INVALID_ARG;
        }
        sets.resize(std::max<size_t>(sets.size(), size_t(set.space) + 1));
    }
    const uint64_t globalSpace = getElementSpace(m_programLayout->getGlobalParamsVarLayout(), m_elementTypeLayout);
    SLANG_RETURN_ON_FAIL(placeChildren(
        m_device,
        m_elementTypeLayout,
        globalSpace,
        m_subObjectRanges,
        m_bindingRanges,
        sets,
        placements->root
    ));
    placements->entryPoints.resize(m_entryPoints.size());
    for (size_t i = 0; i < m_entryPoints.size(); ++i)
    {
        auto* layout = m_entryPoints[i].layout.get();
        const uint64_t entryPointSpace =
            getElementSpace(layout->getSlangLayout()->getVarLayout(), layout->getElementTypeLayout());
        SLANG_RETURN_ON_FAIL(placeChildren(
            m_device,
            layout->getElementTypeLayout(),
            entryPointSpace,
            layout->getSubObjectRanges(),
            layout->getBindingRanges(),
            sets,
            placements->entryPoints[i]
        ));
    }
    m_descriptorSetPlacements = std::move(placements);
    return SLANG_OK;
}

} // namespace rhi::vk
