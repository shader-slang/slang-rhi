#include "metal-shader-object-layout.h"

namespace rhi::metal {

static slang::TypeLayoutReflection* _getParameterBlockTypeLayout(
    slang::ISession* slangSession,
    slang::TypeLayoutReflection* elementTypeLayout
)
{
    return slangSession->getTypeLayout(elementTypeLayout->getType(), 0, slang::LayoutRules::MetalArgumentBufferTier2);
}

ShaderObjectLayoutImpl::SubObjectRangeOffset::SubObjectRangeOffset(slang::VariableLayoutReflection* varLayout)
    : BindingOffset(varLayout)
{
}

ShaderObjectLayoutImpl::SubObjectRangeStride::SubObjectRangeStride(slang::TypeLayoutReflection* typeLayout)
    : BindingOffset(typeLayout)
{
}

Result ShaderObjectLayoutImpl::Builder::setElementTypeLayout(slang::TypeLayoutReflection* typeLayout)
{
    typeLayout = _unwrapParameterGroups(typeLayout, m_containerType);

    m_elementTypeLayout = typeLayout;

    if (m_containerType == ShaderObjectContainerType::ParameterBlock)
    {
        m_parameterBlockTypeLayout = _getParameterBlockTypeLayout(m_session, m_elementTypeLayout);

        // If we have a parameter-block, we should be working on the `ParameterBlockTypeLayout`
        // since this layout will format data for an arg-buffer-tier2 if available.
        typeLayout = m_parameterBlockTypeLayout;
    }
    m_totalOrdinaryDataSize = (uint32_t)typeLayout->getSize();
    if (m_totalOrdinaryDataSize > 0)
    {
        m_resourceCount.buffer++;
    }

    // Compute the binding ranges that are used to store
    // the logical contents of the object in memory.

    SlangInt bindingRangeCount = typeLayout->getBindingRangeCount();
    for (SlangInt r = 0; r < bindingRangeCount; ++r)
    {
        slang::BindingType slangBindingType = typeLayout->getBindingRangeType(r);
        SlangInt count = typeLayout->getBindingRangeBindingCount(r);
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
            slotIndex = m_slotCount;
            if (slangLeafTypeLayout->getType()->getElementType() != nullptr)
            {
                // A structured buffer occupies both a resource slot and
                // a sub-object slot.
                subObjectIndex = m_subObjectCount;
                m_subObjectCount += count;
            }
            m_slotCount += count;
            m_resourceCount.buffer += count;
            break;
        case slang::BindingType::Sampler:
            slotIndex = m_slotCount;
            m_slotCount += count;
            m_resourceCount.sampler += count;
            break;
        case slang::BindingType::Texture:
        case slang::BindingType::MutableTexture:
            slotIndex = m_slotCount;
            m_slotCount += count;
            m_resourceCount.texture += count;
            break;
        case slang::BindingType::TypedBuffer:
        case slang::BindingType::MutableTypedBuffer:
            slotIndex = m_slotCount;
            m_slotCount += count;
            m_resourceCount.buffer += count;
            break;
        default:
            break;
        }

        BindingRangeInfo bindingRangeInfo;
        bindingRangeInfo.bindingType = slangBindingType;
        bindingRangeInfo.count = count;
        bindingRangeInfo.slotIndex = slotIndex;
        bindingRangeInfo.subObjectIndex = subObjectIndex;

        // We'd like to extract the information on the Metal resource
        // index that this range should bind into.
        //
        // A binding range represents a logical member of the shader
        // object type, and it may encompass zero or more *descriptor
        // ranges* that describe how it is physically bound to pipeline
        // state.
        //
        // If the current binding range is backed by at least one descriptor
        // range then we can query the register offset of that descriptor
        // range. We expect that in the common case there will be exactly
        // one descriptor range, and we can extract the information easily.
        //
        // TODO: we might eventually need to special-case our handling
        // of combined texture-sampler ranges since they will need to
        // store two different offsets.
        //
        if (typeLayout->getBindingRangeDescriptorRangeCount(r) != 0)
        {
            // The Slang reflection information organizes the descriptor ranges
            // into "descriptor sets" but Metal has no notion like that so we
            // expect all ranges belong to a single set.
            //
            SlangInt descriptorSetIndex = typeLayout->getBindingRangeDescriptorSetIndex(r);
            SLANG_RHI_ASSERT(descriptorSetIndex == 0);

            SlangInt descriptorRangeIndex = typeLayout->getBindingRangeFirstDescriptorRangeIndex(r);
            auto registerOffset =
                typeLayout->getDescriptorSetDescriptorRangeIndexOffset(descriptorSetIndex, descriptorRangeIndex);

            bindingRangeInfo.registerOffset = (uint32_t)registerOffset;
        }

        m_bindingRanges.push_back(bindingRangeInfo);
    }

    m_totalResourceCount = m_resourceCount;

    SlangInt subObjectRangeCount = typeLayout->getSubObjectRangeCount();
    for (SlangInt r = 0; r < subObjectRangeCount; ++r)
    {
        SlangInt bindingRangeIndex = typeLayout->getSubObjectRangeBindingRangeIndex(r);

        auto slangBindingType = typeLayout->getBindingRangeType(bindingRangeIndex);
        slang::TypeLayoutReflection* slangLeafTypeLayout = typeLayout->getBindingRangeLeafTypeLayout(bindingRangeIndex);

        SubObjectRangeInfo subObjectRange;
        subObjectRange.bindingRangeIndex = bindingRangeIndex;

        // We will use Slang reflection information to extract the offset and stride
        // information for each sub-object range.
        //
        subObjectRange.offset = SubObjectRangeOffset(typeLayout->getSubObjectRangeOffset(r));
        subObjectRange.stride = SubObjectRangeStride(slangLeafTypeLayout);

        // A sub-object range can either represent a sub-object of a known
        // type, like a `ConstantBuffer<Foo>` or `ParameterBlock<Foo>`
        // *or* it can represent a sub-object of some existential type (e.g., `IBar`).
        //
        RefPtr<ShaderObjectLayoutImpl> subObjectLayout;
        switch (slangBindingType)
        {
        default:
        {
            auto elementTypeLayout = slangLeafTypeLayout->getElementTypeLayout();
            createForElementType(m_device, m_session, elementTypeLayout, subObjectLayout.writeRef());
        }
        break;
        case slang::BindingType::ConstantBuffer:
        {
            // In the case of `ConstantBuffer<X>` or `cbuffer`
            // we can construct a layout from the element type directly.
            auto elementTypeLayout = slangLeafTypeLayout->getElementTypeLayout();
            createForElementType(m_device, m_session, elementTypeLayout, subObjectLayout.writeRef());
            break;
        }
        case slang::BindingType::ParameterBlock:
            // On metal, ParameterBlock is represented as a single argument buffer.
            // We will let _unwrapParameterGroups to handle the dereference logic.
            createForElementType(m_device, m_session, slangLeafTypeLayout, subObjectLayout.writeRef());
            break;
        case slang::BindingType::ExistentialValue:
            // In the case of an interface-type sub-object range, we can only
            // construct a layout if we have static specialization information
            // that tells us what type we expect to find in that range.
            //
            // Pending data layout APIs have been removed.
            // Interface-type ranges now have no additional layout information.
            // Sub-object layout remains nullptr for interface types.
            break;
        }
        subObjectRange.layout = subObjectLayout;

        m_subObjectRanges.push_back(subObjectRange);

        if (subObjectLayout && slangBindingType != slang::BindingType::ParameterBlock)
        {
            m_totalResourceCount += subObjectLayout->m_totalResourceCount;
        }
    }
    return SLANG_OK;
}

Result ShaderObjectLayoutImpl::Builder::build(ShaderObjectLayoutImpl** outLayout)
{
    auto layout = RefPtr<ShaderObjectLayoutImpl>(new ShaderObjectLayoutImpl());
    SLANG_RETURN_ON_FAIL(layout->_init(this));

    returnRefPtrMove(outLayout, layout);
    return SLANG_OK;
}

slang::TypeLayoutReflection* ShaderObjectLayoutImpl::getParameterBlockTypeLayout()
{
    if (!m_parameterBlockTypeLayout)
        m_parameterBlockTypeLayout = _getParameterBlockTypeLayout(m_slangSession.get(), m_elementTypeLayout);
    return m_parameterBlockTypeLayout;
}

Result ShaderObjectLayoutImpl::createForElementType(
    Device* device,
    slang::ISession* session,
    slang::TypeLayoutReflection* elementType,
    ShaderObjectLayoutImpl** outLayout
)
{
    Builder builder(device, session);
    builder.setElementTypeLayout(elementType);
    return builder.build(outLayout);
}

Result ShaderObjectLayoutImpl::_init(const Builder* builder)
{
    auto device = builder->m_device;

    initBase(device, builder->m_session, builder->m_elementTypeLayout);

    m_parameterBlockTypeLayout = builder->m_parameterBlockTypeLayout;
    m_slotCount = builder->m_slotCount;
    m_subObjectCount = builder->m_subObjectCount;
    m_resourceCount = builder->m_resourceCount;
    m_totalResourceCount = builder->m_totalResourceCount;

    m_bindingRanges = builder->m_bindingRanges;
    m_subObjectRanges = builder->m_subObjectRanges;

    m_totalOrdinaryDataSize = builder->m_totalOrdinaryDataSize;

    m_containerType = builder->m_containerType;
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
}

void RootShaderObjectLayoutImpl::Builder::addEntryPoint(
    SlangStage stage,
    ShaderObjectLayoutImpl* entryPointLayout,
    slang::EntryPointLayout* slangEntryPoint
)
{
    EntryPointInfo info;
    info.layout = entryPointLayout;
    info.offset = BindingOffset(slangEntryPoint->getVarLayout());
    m_entryPoints.push_back(info);
    m_totalResourceCount += entryPointLayout->m_totalResourceCount;
}

Result RootShaderObjectLayoutImpl::create(
    Device* device,
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
        RefPtr<ShaderObjectLayoutImpl> entryPointLayout;
        SLANG_RETURN_ON_FAIL(
            ShaderObjectLayoutImpl::createForElementType(
                device,
                program->getSession(),
                slangEntryPoint->getTypeLayout(),
                entryPointLayout.writeRef()
            )
        );
        builder.addEntryPoint(slangEntryPoint->getStage(), entryPointLayout, slangEntryPoint);
    }

    SLANG_RETURN_ON_FAIL(builder.build(outLayout));

    return SLANG_OK;
}

Result RootShaderObjectLayoutImpl::_init(const Builder* builder)
{
    SLANG_RETURN_ON_FAIL(Super::_init(builder));

    m_program = builder->m_program;
    m_programLayout = builder->m_programLayout;
    m_entryPoints = builder->m_entryPoints;
    m_slangSession = m_program->getSession();

    return SLANG_OK;
}

} // namespace rhi::metal
