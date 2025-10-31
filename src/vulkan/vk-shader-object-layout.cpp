#include "vk-shader-object-layout.h"
#include "vk-device.h"
#include "vk-bindless-descriptor-set.h"
#include "vk-utils.h"

namespace rhi::vk {

uint32_t ShaderObjectLayoutImpl::Builder::findOrAddDescriptorSet(uint32_t space)
{
    auto it = m_mapSpaceToDescriptorSetIndex.find(space);
    if (it != m_mapSpaceToDescriptorSetIndex.end())
        return it->second;

    DescriptorSetInfo info = {};
    info.space = space;

    uint32_t index = m_descriptorSetBuildInfos.size();
    m_descriptorSetBuildInfos.push_back(info);

    m_mapSpaceToDescriptorSetIndex.emplace(space, index);
    return index;
}

VkDescriptorType ShaderObjectLayoutImpl::Builder::_mapDescriptorType(slang::BindingType slangBindingType)
{
    switch (slangBindingType)
    {
    case slang::BindingType::PushConstant:
    default:
        SLANG_RHI_ASSERT_FAILURE("Unsupported binding type");
        return VK_DESCRIPTOR_TYPE_MAX_ENUM;

    case slang::BindingType::Sampler:
        return VK_DESCRIPTOR_TYPE_SAMPLER;
    case slang::BindingType::CombinedTextureSampler:
        return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    case slang::BindingType::Texture:
        return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    case slang::BindingType::MutableTexture:
        return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    case slang::BindingType::TypedBuffer:
        return VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    case slang::BindingType::MutableTypedBuffer:
        return VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
    case slang::BindingType::RawBuffer:
    case slang::BindingType::MutableRawBuffer:
        return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    case slang::BindingType::InputRenderTarget:
        return VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    case slang::BindingType::InlineUniformData:
        return VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT;
    case slang::BindingType::RayTracingAccelerationStructure:
        return VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    case slang::BindingType::ConstantBuffer:
        return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    }
}

/// Add any descriptor ranges implied by this object containing a leaf
/// sub-object described by `typeLayout`, at the given `offset`.

void ShaderObjectLayoutImpl::Builder::_addDescriptorRangesAsValue(
    slang::TypeLayoutReflection* typeLayout,
    const BindingOffset& offset
)
{
    // First we will scan through all the descriptor sets that the Slang reflection
    // information believes go into making up the given type.
    //
    // Note: We are initializing the sets in order so that their order in our
    // internal data structures should be deterministically based on the order
    // in which they are listed in Slang's reflection information.
    //
    uint32_t descriptorSetCount = typeLayout->getDescriptorSetCount();
    for (uint32_t i = 0; i < descriptorSetCount; ++i)
    {
        SlangInt descriptorRangeCount = typeLayout->getDescriptorSetDescriptorRangeCount(i);
        if (descriptorRangeCount == 0)
            continue;
        auto descriptorSetIndex =
            findOrAddDescriptorSet(offset.bindingSet + typeLayout->getDescriptorSetSpaceOffset(i));
        SLANG_UNUSED(descriptorSetIndex);
    }

    // For actually populating the descriptor sets we prefer to enumerate
    // the binding ranges of the type instead of the descriptor sets.
    //
    uint32_t bindRangeCount = typeLayout->getBindingRangeCount();
    for (uint32_t i = 0; i < bindRangeCount; ++i)
    {
        auto bindingRangeIndex = i;
        auto bindingRangeType = typeLayout->getBindingRangeType(bindingRangeIndex);
        switch (bindingRangeType)
        {
        default:
            break;

        // We will skip over ranges that represent sub-objects for now, and handle
        // them in a separate pass.
        //
        case slang::BindingType::ParameterBlock:
        case slang::BindingType::ConstantBuffer:
        case slang::BindingType::ExistentialValue:
        case slang::BindingType::PushConstant:
            continue;

        case slang::BindingType::VaryingInput:
        case slang::BindingType::VaryingOutput:
            continue;
        }

        // Given a binding range we are interested in, we will then enumerate
        // its contained descriptor ranges.

        uint32_t descriptorRangeCount = typeLayout->getBindingRangeDescriptorRangeCount(bindingRangeIndex);
        if (descriptorRangeCount == 0)
            continue;
        auto slangDescriptorSetIndex = typeLayout->getBindingRangeDescriptorSetIndex(bindingRangeIndex);
        auto descriptorSetIndex = findOrAddDescriptorSet(
            offset.bindingSet + typeLayout->getDescriptorSetSpaceOffset(slangDescriptorSetIndex)
        );
        auto& descriptorSetInfo = m_descriptorSetBuildInfos[descriptorSetIndex];

        uint32_t firstDescriptorRangeIndex = typeLayout->getBindingRangeFirstDescriptorRangeIndex(bindingRangeIndex);
        for (uint32_t j = 0; j < descriptorRangeCount; ++j)
        {
            uint32_t descriptorRangeIndex = firstDescriptorRangeIndex + j;
            auto slangDescriptorType =
                typeLayout->getDescriptorSetDescriptorRangeType(slangDescriptorSetIndex, descriptorRangeIndex);

            // Certain kinds of descriptor ranges reflected by Slang do not
            // manifest as descriptors at the Vulkan level, so we will skip those.
            //
            switch (slangDescriptorType)
            {
            case slang::BindingType::ExistentialValue:
            case slang::BindingType::InlineUniformData:
            case slang::BindingType::PushConstant:
                continue;
            default:
                break;
            }

            auto vkDescriptorType = _mapDescriptorType(slangDescriptorType);
            VkDescriptorSetLayoutBinding vkBindingRangeDesc = {};
            vkBindingRangeDesc.binding =
                offset.binding + (uint32_t)typeLayout->getDescriptorSetDescriptorRangeIndexOffset(
                                     slangDescriptorSetIndex,
                                     descriptorRangeIndex
                                 );
            vkBindingRangeDesc.descriptorCount = (uint32_t)typeLayout->getDescriptorSetDescriptorRangeDescriptorCount(
                slangDescriptorSetIndex,
                descriptorRangeIndex
            );
            vkBindingRangeDesc.descriptorType = vkDescriptorType;
            vkBindingRangeDesc.stageFlags = VK_SHADER_STAGE_ALL;

            descriptorSetInfo.vkBindings.push_back(vkBindingRangeDesc);
        }
    }

    // We skipped over the sub-object ranges when adding descriptors above,
    // and now we will address that oversight by iterating over just
    // the sub-object ranges.
    //
    uint32_t subObjectRangeCount = typeLayout->getSubObjectRangeCount();
    for (uint32_t subObjectRangeIndex = 0; subObjectRangeIndex < subObjectRangeCount; ++subObjectRangeIndex)
    {
        auto bindingRangeIndex = typeLayout->getSubObjectRangeBindingRangeIndex(subObjectRangeIndex);
        auto bindingType = typeLayout->getBindingRangeType(bindingRangeIndex);

        auto subObjectTypeLayout = typeLayout->getBindingRangeLeafTypeLayout(bindingRangeIndex);
        SLANG_RHI_ASSERT(subObjectTypeLayout);

        BindingOffset subObjectRangeOffset = offset;
        subObjectRangeOffset += BindingOffset(typeLayout->getSubObjectRangeOffset(subObjectRangeIndex));

        switch (bindingType)
        {
        // A `ParameterBlock<X>` never contributes descripto ranges to the
        // decriptor sets of a parent object.
        //
        case slang::BindingType::ParameterBlock:
        default:
            break;

        case slang::BindingType::ExistentialValue:
            // Interface-type ranges are no longer supported after pending data removal.
            break;

        case slang::BindingType::ConstantBuffer:
        {
            // A `ConstantBuffer<X>` range will contribute any nested descriptor
            // ranges in `X`, along with a leading descriptor range for a
            // uniform buffer to hold ordinary/uniform data, if there is any.

            SLANG_RHI_ASSERT(subObjectTypeLayout);

            auto containerVarLayout = subObjectTypeLayout->getContainerVarLayout();
            SLANG_RHI_ASSERT(containerVarLayout);

            auto elementVarLayout = subObjectTypeLayout->getElementVarLayout();
            SLANG_RHI_ASSERT(elementVarLayout);

            auto elementTypeLayout = elementVarLayout->getTypeLayout();
            SLANG_RHI_ASSERT(elementTypeLayout);

            BindingOffset containerOffset = subObjectRangeOffset;
            containerOffset += BindingOffset(subObjectTypeLayout->getContainerVarLayout());

            BindingOffset elementOffset = subObjectRangeOffset;
            elementOffset += BindingOffset(elementVarLayout);

            _addDescriptorRangesAsConstantBuffer(elementTypeLayout, containerOffset, elementOffset);
        }
        break;

        case slang::BindingType::PushConstant:
        {
            // This case indicates a `ConstantBuffer<X>` that was marked as being
            // used for push constants.
            //
            // Much of the handling is the same as for an ordinary
            // `ConstantBuffer<X>`, but of course we need to handle the ordinary
            // data part differently.

            SLANG_RHI_ASSERT(subObjectTypeLayout);

            auto containerVarLayout = subObjectTypeLayout->getContainerVarLayout();
            SLANG_RHI_ASSERT(containerVarLayout);

            auto elementVarLayout = subObjectTypeLayout->getElementVarLayout();
            SLANG_RHI_ASSERT(elementVarLayout);

            auto elementTypeLayout = elementVarLayout->getTypeLayout();
            SLANG_RHI_ASSERT(elementTypeLayout);

            BindingOffset containerOffset = subObjectRangeOffset;
            containerOffset += BindingOffset(subObjectTypeLayout->getContainerVarLayout());

            BindingOffset elementOffset = subObjectRangeOffset;
            elementOffset += BindingOffset(elementVarLayout);

            _addDescriptorRangesAsPushConstantBuffer(elementTypeLayout, containerOffset, elementOffset);
        }
        break;
        }
    }
}

/// Add the descriptor ranges implied by a `ConstantBuffer<X>` where `X` is
/// described by `elementTypeLayout`.
///
/// The `containerOffset` and `elementOffset` are the binding offsets that
/// should apply to the buffer itself and the contents of the buffer, respectively.
///

void ShaderObjectLayoutImpl::Builder::_addDescriptorRangesAsConstantBuffer(
    slang::TypeLayoutReflection* elementTypeLayout,
    const BindingOffset& containerOffset,
    const BindingOffset& elementOffset
)
{
    // If the type has ordinary uniform data fields, we need to make sure to create
    // a descriptor set with a constant buffer binding in the case that the shader
    // object is bound as a stand alone parameter block.
    if (elementTypeLayout->getSize(SLANG_PARAMETER_CATEGORY_UNIFORM) != 0)
    {
        auto descriptorSetIndex = findOrAddDescriptorSet(containerOffset.bindingSet);
        auto& descriptorSetInfo = m_descriptorSetBuildInfos[descriptorSetIndex];
        VkDescriptorSetLayoutBinding vkBindingRangeDesc = {};
        vkBindingRangeDesc.binding = containerOffset.binding;
        vkBindingRangeDesc.descriptorCount = 1;
        vkBindingRangeDesc.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        vkBindingRangeDesc.stageFlags = VK_SHADER_STAGE_ALL;
        descriptorSetInfo.vkBindings.push_back(vkBindingRangeDesc);
    }

    _addDescriptorRangesAsValue(elementTypeLayout, elementOffset);
}

/// Add the descriptor ranges implied by a `PushConstantBuffer<X>` where `X` is
/// described by `elementTypeLayout`.
///
/// The `containerOffset` and `elementOffset` are the binding offsets that
/// should apply to the buffer itself and the contents of the buffer, respectively.
///

void ShaderObjectLayoutImpl::Builder::_addDescriptorRangesAsPushConstantBuffer(
    slang::TypeLayoutReflection* elementTypeLayout,
    const BindingOffset& containerOffset,
    const BindingOffset& elementOffset
)
{
    // If the type has ordinary uniform data fields, we need to make sure to create
    // a descriptor set with a constant buffer binding in the case that the shader
    // object is bound as a stand alone parameter block.
    auto ordinaryDataSize = (uint32_t)elementTypeLayout->getSize(SLANG_PARAMETER_CATEGORY_UNIFORM);
    if (ordinaryDataSize != 0)
    {
        auto pushConstantRangeIndex = containerOffset.pushConstantRange;

        VkPushConstantRange vkPushConstantRange = {};
        vkPushConstantRange.size = ordinaryDataSize;
        vkPushConstantRange.stageFlags = VK_SHADER_STAGE_ALL; // TODO: be more clever

        while ((uint32_t)m_ownPushConstantRanges.size() <= pushConstantRangeIndex)
        {
            VkPushConstantRange emptyRange = {0};
            m_ownPushConstantRanges.push_back(emptyRange);
        }

        m_ownPushConstantRanges[pushConstantRangeIndex] = vkPushConstantRange;
    }

    _addDescriptorRangesAsValue(elementTypeLayout, elementOffset);
}

/// Add binding ranges to this shader object layout, as implied by the given
/// `typeLayout`

void ShaderObjectLayoutImpl::Builder::addBindingRanges(slang::TypeLayoutReflection* typeLayout)
{
    SlangInt bindingRangeCount = typeLayout->getBindingRangeCount();
    for (SlangInt r = 0; r < bindingRangeCount; ++r)
    {
        slang::BindingType slangBindingType = typeLayout->getBindingRangeType(r);
        uint32_t count = (uint32_t)typeLayout->getBindingRangeBindingCount(r);
        slang::TypeLayoutReflection* slangLeafTypeLayout = typeLayout->getBindingRangeLeafTypeLayout(r);

        uint32_t slotIndex = 0;
        uint32_t subObjectIndex = 0;

        switch (slangBindingType)
        {
        case slang::BindingType::ConstantBuffer:
        case slang::BindingType::ParameterBlock:
        case slang::BindingType::ExistentialValue:
            subObjectIndex = m_subObjectCount;
            m_subObjectCount += count;
            break;
        case slang::BindingType::RawBuffer:
        case slang::BindingType::MutableRawBuffer:
            if (slangLeafTypeLayout->getType()->getElementType() != nullptr)
            {
                // A structured buffer occupies both a resource slot and
                // a sub-object slot.
                subObjectIndex = m_subObjectCount;
                m_subObjectCount += count;
            }
            slotIndex = m_slotCount;
            m_slotCount += count;
            break;
        case slang::BindingType::Sampler:
            slotIndex = m_slotCount;
            m_slotCount += count;
            m_totalBindingCount += 1;
            break;

        case slang::BindingType::CombinedTextureSampler:
            slotIndex = m_slotCount;
            m_slotCount += count;
            m_totalBindingCount += 1;
            break;

        case slang::BindingType::VaryingInput:
        case slang::BindingType::VaryingOutput:
            break;

        default:
            slotIndex = m_slotCount;
            m_slotCount += count;
            m_totalBindingCount += 1;
            break;
        }

        BindingRangeInfo bindingRangeInfo;
        bindingRangeInfo.bindingType = slangBindingType;
        bindingRangeInfo.count = count;
        bindingRangeInfo.slotIndex = slotIndex;
        bindingRangeInfo.subObjectIndex = subObjectIndex;
        bindingRangeInfo.isSpecializable = typeLayout->isBindingRangeSpecializable(r);
        // We'd like to extract the information on the GLSL/SPIR-V
        // `binding` that this range should bind into (or whatever
        // other specific kind of offset/index is appropriate to it).
        //
        // A binding range represents a logical member of the shader
        // object type, and it may encompass zero or more *descriptor
        // ranges* that describe how it is physically bound to pipeline
        // state.
        //
        // If the current bindign range is backed by at least one descriptor
        // range then we can query the binding offset of that descriptor
        // range. We expect that in the common case there will be exactly
        // one descriptor range, and we can extract the information easily.
        //
        if (typeLayout->getBindingRangeDescriptorRangeCount(r) != 0)
        {
            SlangInt descriptorSetIndex = typeLayout->getBindingRangeDescriptorSetIndex(r);
            SlangInt descriptorRangeIndex = typeLayout->getBindingRangeFirstDescriptorRangeIndex(r);

            auto set = typeLayout->getDescriptorSetSpaceOffset(descriptorSetIndex);
            auto bindingOffset =
                typeLayout->getDescriptorSetDescriptorRangeIndexOffset(descriptorSetIndex, descriptorRangeIndex);

            bindingRangeInfo.setOffset = uint32_t(set);
            bindingRangeInfo.bindingOffset = uint32_t(bindingOffset);
        }

        m_bindingRanges.push_back(bindingRangeInfo);
    }

    SlangInt subObjectRangeCount = typeLayout->getSubObjectRangeCount();
    for (SlangInt r = 0; r < subObjectRangeCount; ++r)
    {
        SlangInt bindingRangeIndex = typeLayout->getSubObjectRangeBindingRangeIndex(r);
        auto slangBindingType = typeLayout->getBindingRangeType(bindingRangeIndex);
        slang::TypeLayoutReflection* slangLeafTypeLayout = typeLayout->getBindingRangeLeafTypeLayout(bindingRangeIndex);

        // A sub-object range can either represent a sub-object of a known
        // type, like a `ConstantBuffer<Foo>` or `ParameterBlock<Foo>`
        // (in which case we can pre-compute a layout to use, based on
        // the type `Foo`) *or* it can represent a sub-object of some
        // existential type (e.g., `IBar`) in which case we cannot
        // know the appropraite type/layout of sub-object to allocate.
        //
        RefPtr<ShaderObjectLayoutImpl> subObjectLayout;
        switch (slangBindingType)
        {
        default:
        {
            auto varLayout = slangLeafTypeLayout->getElementVarLayout();
            auto subTypeLayout = varLayout->getTypeLayout();
            ShaderObjectLayoutImpl::createForElementType(
                m_device,
                m_session,
                subTypeLayout,
                subObjectLayout.writeRef()
            );
        }
        break;

        case slang::BindingType::ExistentialValue:
            break;
        }

        SubObjectRangeInfo subObjectRange;
        subObjectRange.bindingRangeIndex = bindingRangeIndex;
        subObjectRange.layout = subObjectLayout;

        // We will use Slang reflection infromation to extract the offset information
        // for each sub-object range.
        //
        // TODO: We should also be extracting the uniform offset here.
        //
        subObjectRange.offset = SubObjectRangeOffset(typeLayout->getSubObjectRangeOffset(r));
        subObjectRange.stride = SubObjectRangeStride(slangLeafTypeLayout);

        switch (slangBindingType)
        {
        case slang::BindingType::ParameterBlock:
            m_childDescriptorSetCount += subObjectLayout->getTotalDescriptorSetCount();
            m_childPushConstantRangeCount += subObjectLayout->getTotalPushConstantRangeCount();
            break;

        case slang::BindingType::ConstantBuffer:
            m_childDescriptorSetCount += subObjectLayout->getChildDescriptorSetCount();
            m_totalBindingCount += subObjectLayout->getTotalBindingCount();
            m_childPushConstantRangeCount += subObjectLayout->getTotalPushConstantRangeCount();
            break;

        case slang::BindingType::ExistentialValue:
            if (subObjectLayout)
            {
                m_childDescriptorSetCount += subObjectLayout->getChildDescriptorSetCount();
                m_totalBindingCount += subObjectLayout->getTotalBindingCount();
                m_childPushConstantRangeCount += subObjectLayout->getTotalPushConstantRangeCount();

                // Interface-type ranges are no longer supported after pending data removal.
            }
            break;

        default:
            break;
        }

        m_subObjectRanges.push_back(subObjectRange);
    }
}

Result ShaderObjectLayoutImpl::Builder::setElementTypeLayout(slang::TypeLayoutReflection* typeLayout)
{
    typeLayout = _unwrapParameterGroups(typeLayout, m_containerType);
    m_elementTypeLayout = typeLayout;

    m_totalOrdinaryDataSize = (uint32_t)typeLayout->getSize();

    // Next we will compute the binding ranges that are used to store
    // the logical contents of the object in memory. These will relate
    // to the descriptor ranges in the various sets, but not always
    // in a one-to-one fashion.

    addBindingRanges(typeLayout);

    // Note: This routine does not take responsibility for
    // adding descriptor ranges at all, because the exact way
    // that descriptor ranges need to be added varies between
    // ordinary shader objects, root shader objects, and entry points.

    return SLANG_OK;
}

Result ShaderObjectLayoutImpl::Builder::build(ShaderObjectLayoutImpl** outLayout)
{
    auto layout = RefPtr<ShaderObjectLayoutImpl>(new ShaderObjectLayoutImpl());
    SLANG_RETURN_ON_FAIL(layout->_init(this));

    returnRefPtrMove(outLayout, layout);
    return SLANG_OK;
}

Result ShaderObjectLayoutImpl::createForElementType(
    DeviceImpl* device,
    slang::ISession* session,
    slang::TypeLayoutReflection* elementType,
    ShaderObjectLayoutImpl** outLayout
)
{
    Builder builder(device, session);
    builder.setElementTypeLayout(elementType);

    // When constructing a shader object layout directly from a reflected
    // type in Slang, we want to compute the descriptor sets and ranges
    // that would be used if this object were bound as a parameter block.
    //
    // It might seem like we need to deal with the other cases for how
    // the shader object might be bound, but the descriptor ranges we
    // compute here will only ever be used in parameter-block case.
    //
    // One important wrinkle is that we know that the parameter block
    // allocated for `elementType` will potentially need a buffer `binding`
    // for any ordinary data it contains.

    bool needsOrdinaryDataBuffer = builder.m_elementTypeLayout->getSize(SLANG_PARAMETER_CATEGORY_UNIFORM) != 0;
    uint32_t ordinaryDataBufferCount = needsOrdinaryDataBuffer ? 1 : 0;

    // When binding the object, we know that the ordinary data buffer will
    // always use a the first available `binding`, so its offset will be
    // all zeroes.
    //
    BindingOffset containerOffset;

    // In contrast, the `binding`s used by all the other entries in the
    // parameter block will need to be offset by one if there was
    // an ordinary data buffer.
    //
    BindingOffset elementOffset;
    elementOffset.binding = ordinaryDataBufferCount;

    // Once we've computed the offset information, we simply add the
    // descriptor ranges as if things were declared as a `ConstantBuffer<X>`,
    // since that is how things will be laid out inside the parameter block.
    //
    builder._addDescriptorRangesAsConstantBuffer(builder.m_elementTypeLayout, containerOffset, elementOffset);
    return builder.build(outLayout);
}

ShaderObjectLayoutImpl::~ShaderObjectLayoutImpl()
{
    for (auto& descSetInfo : m_descriptorSetInfos)
    {
        getDevice()
            ->m_api.vkDestroyDescriptorSetLayout(getDevice()->m_api.m_device, descSetInfo.descriptorSetLayout, nullptr);
    }
}

Result ShaderObjectLayoutImpl::_init(const Builder* builder)
{
    auto device = builder->m_device;

    initBase(device, builder->m_session, builder->m_elementTypeLayout);

    m_bindingRanges = builder->m_bindingRanges;

    m_descriptorSetInfos = _Move(builder->m_descriptorSetBuildInfos);
    m_ownPushConstantRanges = builder->m_ownPushConstantRanges;
    m_slotCount = builder->m_slotCount;
    m_childDescriptorSetCount = builder->m_childDescriptorSetCount;
    m_totalBindingCount = builder->m_totalBindingCount;
    m_subObjectCount = builder->m_subObjectCount;
    m_subObjectRanges = builder->m_subObjectRanges;
    m_totalOrdinaryDataSize = builder->m_totalOrdinaryDataSize;

    m_containerType = builder->m_containerType;

    // Create VkDescriptorSetLayout for all descriptor sets.
    for (auto& descriptorSetInfo : m_descriptorSetInfos)
    {
        VkDescriptorSetLayoutCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        createInfo.pBindings = descriptorSetInfo.vkBindings.data();
        createInfo.bindingCount = (uint32_t)descriptorSetInfo.vkBindings.size();
        VkDescriptorSetLayout vkDescSetLayout;
        SLANG_RETURN_ON_FAIL(
            device->m_api.vkCreateDescriptorSetLayout(device->m_api.m_device, &createInfo, nullptr, &vkDescSetLayout)
        );
        descriptorSetInfo.descriptorSetLayout = vkDescSetLayout;
    }
    return SLANG_OK;
}

DeviceImpl* ShaderObjectLayoutImpl::getDevice()
{
    return checked_cast<DeviceImpl*>(m_device);
}

Result EntryPointLayout::Builder::build(EntryPointLayout** outLayout)
{
    RefPtr<EntryPointLayout> layout = new EntryPointLayout();
    SLANG_RETURN_ON_FAIL(layout->_init(this));

    returnRefPtrMove(outLayout, layout);
    return SLANG_OK;
}

void EntryPointLayout::Builder::addEntryPointParams(slang::EntryPointLayout* entryPointLayout)
{
    m_slangEntryPointLayout = entryPointLayout;
    setElementTypeLayout(entryPointLayout->getTypeLayout());
    m_shaderStageFlag = translateShaderStage(entryPointLayout->getStage());

    // Note: we do not bother adding any descriptor sets/ranges here,
    // because the descriptor ranges of an entry point will simply
    // be allocated as part of the descriptor sets for the root
    // shader object.
}

Result EntryPointLayout::_init(const Builder* builder)
{
    SLANG_RETURN_ON_FAIL(Super::_init(builder));

    m_slangEntryPointLayout = builder->m_slangEntryPointLayout;
    m_shaderStageFlag = builder->m_shaderStageFlag;
    return SLANG_OK;
}

RootShaderObjectLayoutImpl::~RootShaderObjectLayoutImpl()
{
    if (m_pipelineLayout)
    {
        m_device->m_api.vkDestroyPipelineLayout(m_device->m_api.m_device, m_pipelineLayout, nullptr);
    }
}

uint32_t RootShaderObjectLayoutImpl::findEntryPointIndex(VkShaderStageFlags stage)
{
    auto entryPointCount = m_entryPoints.size();
    for (uint32_t i = 0; i < entryPointCount; ++i)
    {
        auto entryPoint = m_entryPoints[i];
        if (entryPoint.layout->getShaderStageFlag() == stage)
            return i;
    }
    return -1;
}

Result RootShaderObjectLayoutImpl::create(
    DeviceImpl* device,
    slang::IComponentType* program,
    slang::ProgramLayout* programLayout,
    RootShaderObjectLayoutImpl** outLayout
)
{
    RootShaderObjectLayoutImpl::Builder builder(device, program, programLayout);
    builder.addGlobalParams(programLayout->getGlobalParamsVarLayout());

    SlangInt entryPointCount = programLayout->getEntryPointCount();
    for (SlangInt e = 0; e < entryPointCount; ++e)
    {
        auto slangEntryPoint = programLayout->getEntryPointByIndex(e);

        EntryPointLayout::Builder entryPointBuilder(device, program->getSession());
        entryPointBuilder.addEntryPointParams(slangEntryPoint);

        RefPtr<EntryPointLayout> entryPointLayout;
        SLANG_RETURN_ON_FAIL(entryPointBuilder.build(entryPointLayout.writeRef()));

        builder.addEntryPoint(entryPointLayout);
    }

    SLANG_RETURN_ON_FAIL(builder.build(outLayout));

    return SLANG_OK;
}

Result RootShaderObjectLayoutImpl::_init(const Builder* builder)
{
    auto device = builder->m_device;

    SLANG_RETURN_ON_FAIL(Super::_init(builder));

    m_program = builder->m_program;
    m_programLayout = builder->m_programLayout;
    m_entryPoints = _Move(builder->m_entryPoints);
    m_device = device;

    // If the program has unbound specialization parameters,
    // then we will avoid creating a final Vulkan pipeline layout.
    //
    // TODO: We should really create the information necessary
    // for binding as part of a separate object, so that we have
    // a clean seperation between what is needed for writing into
    // a shader object vs. what is needed for binding it to the
    // pipeline. We eventually need to be able to create bindable
    // state objects from unspecialized programs, in order to
    // support dynamic dispatch.
    //
    if (m_program->getSpecializationParamCount() != 0)
        return SLANG_OK;

    // Otherwise, we need to create a final (bindable) layout.
    //
    // We will use a recursive walk to collect all the `VkDescriptorSetLayout`s
    // that are required for the global scope, sub-objects, and entry points.
    //
    SLANG_RETURN_ON_FAIL(addAllDescriptorSets());

    // We will also use a recursive walk to collect all the push-constant
    // ranges needed for this object, sub-objects, and entry points.
    //
    SLANG_RETURN_ON_FAIL(addAllPushConstantRanges());

    // Once we've collected the information across the entire
    // tree of sub-objects

    // Add bindless descriptor set layout if needed.
    // We currently assume that the bindless descriptor set is always the last,
    // following all other descriptor sets, without any gaps.
    if (m_device->m_bindlessDescriptorSet)
    {
        m_vkDescriptorSetLayouts.push_back(m_device->m_bindlessDescriptorSet->m_descriptorSetLayout);
    }

    // Now call Vulkan API to create a pipeline layout.
    VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = {};
    pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutCreateInfo.setLayoutCount = (uint32_t)m_vkDescriptorSetLayouts.size();
    pipelineLayoutCreateInfo.pSetLayouts = m_vkDescriptorSetLayouts.data();
    if (m_allPushConstantRanges.size())
    {
        uint32_t totalPushConstantSize = 0;
        for (const auto& range : m_allPushConstantRanges)
        {
            totalPushConstantSize = std::max(totalPushConstantSize, range.offset + range.size);
        }
        if (totalPushConstantSize > m_device->m_api.m_deviceProperties.limits.maxPushConstantsSize)
        {
            m_device->printError(
                "Total push constant size (%u) exceeds the maximum allowed (%u).",
                totalPushConstantSize,
                m_device->m_api.m_deviceProperties.limits.maxPushConstantsSize
            );
            return SLANG_FAIL;
        }
        pipelineLayoutCreateInfo.pushConstantRangeCount = (uint32_t)m_allPushConstantRanges.size();
        pipelineLayoutCreateInfo.pPushConstantRanges = m_allPushConstantRanges.data();
    }
    SLANG_RETURN_ON_FAIL(
        m_device->m_api
            .vkCreatePipelineLayout(m_device->m_api.m_device, &pipelineLayoutCreateInfo, nullptr, &m_pipelineLayout)
    );
    return SLANG_OK;
}

/// Add all the descriptor sets implied by this root object and sub-objects

Result RootShaderObjectLayoutImpl::addAllDescriptorSets()
{
    SLANG_RETURN_ON_FAIL(addAllDescriptorSetsRec(this));

    // Note: the descriptor ranges/sets for direct entry point parameters
    // were already enumerated into the ranges/sets of the root object itself,
    // so we don't wnat to add them again.
    //
    // We do however have to deal with the possibility that an entry
    // point could introduce "child" descriptor sets, e.g., because it
    // has a `ParameterBlock<X>` parameter.
    //
    for (auto& entryPoint : getEntryPoints())
    {
        SLANG_RETURN_ON_FAIL(addChildDescriptorSetsRec(entryPoint.layout));
    }

    return SLANG_OK;
}

/// Recurisvely add descriptor sets defined by `layout` and sub-objects

Result RootShaderObjectLayoutImpl::addAllDescriptorSetsRec(ShaderObjectLayoutImpl* layout)
{
    // TODO: This logic assumes that descriptor sets are all contiguous
    // and have been allocated in a global order that matches the order
    // of enumeration here.

    for (auto& descSetInfo : layout->getOwnDescriptorSets())
    {
        m_vkDescriptorSetLayouts.push_back(descSetInfo.descriptorSetLayout);
    }

    SLANG_RETURN_ON_FAIL(addChildDescriptorSetsRec(layout));
    return SLANG_OK;
}

/// Recurisvely add descriptor sets defined by sub-objects of `layout`

Result RootShaderObjectLayoutImpl::addChildDescriptorSetsRec(ShaderObjectLayoutImpl* layout)
{
    for (auto& subObject : layout->getSubObjectRanges())
    {
        const auto& bindingRange = layout->getBindingRange(subObject.bindingRangeIndex);
        switch (bindingRange.bindingType)
        {
        case slang::BindingType::ParameterBlock:
            SLANG_RETURN_ON_FAIL(addAllDescriptorSetsRec(subObject.layout));
            break;

        default:
            if (auto subObjectLayout = subObject.layout)
            {
                SLANG_RETURN_ON_FAIL(addChildDescriptorSetsRec(subObject.layout));
            }
            break;
        }
    }

    return SLANG_OK;
}

/// Add all the push-constant ranges implied by this root object and sub-objects

Result RootShaderObjectLayoutImpl::addAllPushConstantRanges()
{
    SLANG_RETURN_ON_FAIL(addAllPushConstantRangesRec(this));

    for (auto& entryPoint : getEntryPoints())
    {
        SLANG_RETURN_ON_FAIL(addChildPushConstantRangesRec(entryPoint.layout));
    }

    return SLANG_OK;
}

/// Recurisvely add push-constant ranges defined by `layout` and sub-objects

Result RootShaderObjectLayoutImpl::addAllPushConstantRangesRec(ShaderObjectLayoutImpl* layout)
{
    // TODO: This logic assumes that push-constant ranges are all contiguous
    // and have been allocated in a global order that matches the order
    // of enumeration here.

    for (auto pushConstantRange : layout->getOwnPushConstantRanges())
    {
        pushConstantRange.offset = m_totalPushConstantSize;
        m_totalPushConstantSize += pushConstantRange.size;

        m_allPushConstantRanges.push_back(pushConstantRange);
    }

    SLANG_RETURN_ON_FAIL(addChildPushConstantRangesRec(layout));
    return SLANG_OK;
}

/// Recurisvely add push-constant ranges defined by sub-objects of `layout`

Result RootShaderObjectLayoutImpl::addChildPushConstantRangesRec(ShaderObjectLayoutImpl* layout)
{
    for (auto& subObject : layout->getSubObjectRanges())
    {
        if (auto subObjectLayout = subObject.layout)
        {
            SLANG_RETURN_ON_FAIL(addAllPushConstantRangesRec(subObject.layout));
        }
    }

    return SLANG_OK;
}

Result RootShaderObjectLayoutImpl::Builder::build(RootShaderObjectLayoutImpl** outLayout)
{
    RefPtr<RootShaderObjectLayoutImpl> layout = new RootShaderObjectLayoutImpl();
    SLANG_RETURN_ON_FAIL(layout->_init(this));
    returnRefPtrMove(outLayout, layout);
    return SLANG_OK;
}

void RootShaderObjectLayoutImpl::Builder::addGlobalParams(slang::VariableLayoutReflection* globalsLayout)
{
    setElementTypeLayout(globalsLayout->getTypeLayout());

    // We need to populate our descriptor sets/ranges with information
    // from the layout of the global scope.
    //
    // While we expect that the parameter in the global scope start
    // at an offset of zero, it is also worth querying the offset
    // information because it could impact the locations assigned
    // for handling static specialization cases.
    //
    BindingOffset offset(globalsLayout);

    // Note: We are adding descriptor ranges here based directly on
    // the type of the global-scope layout. The type layout for the
    // global scope will either be something like a `struct GlobalParams`
    // that contains all the global-scope parameters or a `ConstantBuffer<GlobalParams>`
    // and in either case the `_addDescriptorRangesAsValue` can properly
    // add all the ranges implied.
    //
    // As a result we don't require any special-case logic here to
    // deal with the possibility of a "default" constant buffer allocated
    // for global-scope parameters of uniform/ordinary type.
    //
    _addDescriptorRangesAsValue(globalsLayout->getTypeLayout(), offset);

    // Binding offset handling has been simplified after pending data removal.
    //
}

void RootShaderObjectLayoutImpl::Builder::addEntryPoint(EntryPointLayout* entryPointLayout)
{
    auto slangEntryPointLayout = entryPointLayout->getSlangLayout();
    auto entryPointVarLayout = slangEntryPointLayout->getVarLayout();

    // The offset information for each entry point needs to
    // be handled uniformly now that pending data has been removed.
    // was recorded in the global-scope layout.
    //
    // TODO(tfoley): Double-check that this is correct.

    BindingOffset entryPointOffset(entryPointVarLayout);

    EntryPointInfo info;
    info.layout = entryPointLayout;
    info.offset = entryPointOffset;

    // Similar to the case for the global scope, we expect the
    // type layout for the entry point parameters to be either
    // a `struct EntryPointParams` or a `PushConstantBuffer<EntryPointParams>`.
    // Rather than deal with the different cases here, we will
    // trust the `_addDescriptorRangesAsValue` code to handle
    // either case correctly.
    //
    _addDescriptorRangesAsValue(entryPointVarLayout->getTypeLayout(), entryPointOffset);

    m_entryPoints.push_back(info);

    m_childDescriptorSetCount += entryPointLayout->getTotalDescriptorSetCount();
}

} // namespace rhi::vk
