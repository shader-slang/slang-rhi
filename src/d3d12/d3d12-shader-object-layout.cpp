#include "d3d12-shader-object-layout.h"
#include "d3d12-device.h"

namespace rhi::d3d12 {

inline bool isBindingRangeRootParameter(
    SlangSession* globalSession,
    const char* rootParameterAttributeName,
    slang::TypeLayoutReflection* typeLayout,
    uint32_t bindingRangeIndex
)
{
    bool isRootParameter = false;
    if (rootParameterAttributeName)
    {
        if (auto leafVariable = typeLayout->getBindingRangeLeafVariable(bindingRangeIndex))
        {
            if (leafVariable->findUserAttributeByName(globalSession, rootParameterAttributeName))
            {
                isRootParameter = true;
            }
        }
    }
    return isRootParameter;
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

Result ShaderObjectLayoutImpl::init(Builder* builder)
{
    auto device = builder->m_device;

    initBase(device, builder->m_session, builder->m_elementTypeLayout);

    m_containerType = builder->m_containerType;

    m_bindingRanges = _Move(builder->m_bindingRanges);
    m_subObjectRanges = _Move(builder->m_subObjectRanges);
    m_rootParamsInfo = _Move(builder->m_rootParamsInfo);

    m_ownCounts = builder->m_ownCounts;
    m_totalCounts = builder->m_totalCounts;
    m_slotCount = builder->m_slotCount;
    m_subObjectCount = builder->m_subObjectCount;
    m_childRootParameterCount = builder->m_childRootParameterCount;
    m_totalOrdinaryDataSize = builder->m_totalOrdinaryDataSize;

    return SLANG_OK;
}

Result ShaderObjectLayoutImpl::Builder::setElementTypeLayout(slang::TypeLayoutReflection* typeLayout)
{
    typeLayout = _unwrapParameterGroups(typeLayout, m_containerType);
    m_elementTypeLayout = typeLayout;

    // If the type contains any ordinary data, then we must reserve a buffer
    // descriptor to hold it when binding as a parameter block.
    //
    m_totalOrdinaryDataSize = (uint32_t)typeLayout->getSize();
    if (m_totalOrdinaryDataSize != 0)
    {
        m_ownCounts.resource++;
    }

    // We will scan over the reflected Slang binding ranges and add them
    // to our array. There are two main things we compute along the way:
    //
    // * For each binding range we compute a `flatIndex` that can be
    //   used to identify where the values for the given range begin
    //   in the flattened arrays (e.g., `m_objects`) and descriptor
    //   tables that hold the state of a shader object.
    //
    // * We also update the various counters taht keep track of the number
    //   of sub-objects, resources, samplers, etc. that are being
    //   consumed. These counters will contribute to figuring out
    //   the descriptor table(s) that might be needed to represent
    //   the object.
    //
    SlangInt bindingRangeCount = typeLayout->getBindingRangeCount();
    for (SlangInt r = 0; r < bindingRangeCount; ++r)
    {
        slang::BindingType slangBindingType = typeLayout->getBindingRangeType(r);
        uint32_t count = (uint32_t)typeLayout->getBindingRangeBindingCount(r);
        slang::TypeLayoutReflection* slangLeafTypeLayout = typeLayout->getBindingRangeLeafTypeLayout(r);

        bool isRootParameter = isBindingRangeRootParameter(
            m_device->m_slangContext.globalSession,
            checked_cast<DeviceImpl*>(m_device)->m_extendedDesc.rootParameterShaderAttributeName,
            typeLayout,
            r
        );
        uint32_t bufferElementStride = 0;
        uint32_t slotIndex = 0;
        uint32_t baseIndex = 0;
        uint32_t subObjectIndex = 0;

        switch (slangBindingType)
        {
        case slang::BindingType::RawBuffer:
        case slang::BindingType::TypedBuffer:
        case slang::BindingType::MutableRawBuffer:
        case slang::BindingType::MutableTypedBuffer:
        {
            auto bufferElementType = slangLeafTypeLayout->getElementTypeLayout();
            if (bufferElementType)
            {
                bufferElementStride = (uint32_t)bufferElementType->getStride();
            }
            break;
        }
        default:
            break;
        }
        if (isRootParameter)
        {
            RootParameterInfo rootInfo = {};
            switch (slangBindingType)
            {
            case slang::BindingType::MutableRawBuffer:
            case slang::BindingType::MutableTypedBuffer:
                rootInfo.isUAV = true;
                break;
            default:
                break;
            }
            slotIndex = m_slotCount;
            m_slotCount += count;
            baseIndex = (uint32_t)m_rootParamsInfo.size();
            for (uint32_t i = 0; i < count; i++)
            {
                m_rootParamsInfo.push_back(rootInfo);
            }
        }
        else
        {
            switch (slangBindingType)
            {
            case slang::BindingType::ConstantBuffer:
            case slang::BindingType::ParameterBlock:
            case slang::BindingType::ExistentialValue:
                baseIndex = m_subObjectCount;
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
                baseIndex = m_ownCounts.resource;
                m_ownCounts.resource += count;
                break;
            case slang::BindingType::Sampler:
                slotIndex = m_slotCount;
                m_slotCount += count;
                baseIndex = m_ownCounts.sampler;
                m_ownCounts.sampler += count;
                break;

            case slang::BindingType::CombinedTextureSampler:
                // TODO: support this case...
                break;

            case slang::BindingType::VaryingInput:
            case slang::BindingType::VaryingOutput:
                break;

            default:
                slotIndex = m_slotCount;
                m_slotCount += count;
                baseIndex = m_ownCounts.resource;
                m_ownCounts.resource += count;
                break;
            }
        }

        BindingRangeInfo bindingRangeInfo = {};
        bindingRangeInfo.bindingType = slangBindingType;
        bindingRangeInfo.resourceShape = slangLeafTypeLayout->getResourceShape();
        bindingRangeInfo.count = count;
        bindingRangeInfo.baseIndex = baseIndex;
        bindingRangeInfo.slotIndex = slotIndex;
        bindingRangeInfo.subObjectIndex = subObjectIndex;
        bindingRangeInfo.bufferElementStride = bufferElementStride;
        bindingRangeInfo.isRootParameter = isRootParameter;
        bindingRangeInfo.isSpecializable = typeLayout->isBindingRangeSpecializable(r);

        m_bindingRanges.push_back(bindingRangeInfo);
    }

    // At this point we've computed the number of resources/samplers that
    // the type needs to represent its *own* state, and stored those counts
    // in `m_ownCounts`. Next we need to consider any resources/samplers
    // and root parameters needed to represent the state of the transitive
    // sub-objects of this objet, so that we can compute the total size
    // of the object when bound to the pipeline.

    m_totalCounts = m_ownCounts;

    SlangInt subObjectRangeCount = typeLayout->getSubObjectRangeCount();
    for (SlangInt r = 0; r < subObjectRangeCount; ++r)
    {
        SlangInt bindingRangeIndex = typeLayout->getSubObjectRangeBindingRangeIndex(r);
        auto slangBindingType = typeLayout->getBindingRangeType(bindingRangeIndex);
        auto count = (uint32_t)typeLayout->getBindingRangeBindingCount(bindingRangeIndex);
        slang::TypeLayoutReflection* slangLeafTypeLayout = typeLayout->getBindingRangeLeafTypeLayout(bindingRangeIndex);

        // A sub-object range can either represent a sub-object of a known
        // type, like a `ConstantBuffer<Foo>` or `ParameterBlock<Foo>`
        // (in which case we can pre-compute a layout to use, based on
        // the type `Foo`) *or* it can represent a sub-object of some
        // existential type (e.g., `IBar`) in which case we cannot
        // know the appropraite type/layout of sub-object to allocate.
        //
        RefPtr<ShaderObjectLayoutImpl> subObjectLayout;
        createForElementType(
            m_device,
            m_session,
            slangLeafTypeLayout->getElementTypeLayout(),
            subObjectLayout.writeRef()
        );

        SubObjectRangeInfo subObjectRange;
        subObjectRange.bindingRangeIndex = bindingRangeIndex;
        subObjectRange.layout = subObjectLayout;

        // The offset information is computed based on the counters
        // we are generating here, which depend only on the in-memory layout
        // decisions being made in our implementation. Remember that the
        // `register` and `space` values coming from DXBC/DXIL do *not*
        // dictate the in-memory layout we use.
        //
        // Note: One subtle point here is that the `.rootParam` offset we are computing
        // here does *not* include any root parameters that would be allocated
        // for the parent object type itself (e.g., for descriptor tables
        // used if it were bound as a parameter block). The later logic when
        // we actually go to bind things will need to apply those offsets.
        //
        // Note: An even *more* subtle point is that the `.resource` offset
        // being computed here *does* include the resource descriptor allocated
        // for holding the ordinary data buffer, if any. The implications of
        // this for later offset math is subtle.
        //
        subObjectRange.offset.rootParam = m_childRootParameterCount;
        subObjectRange.offset.resource = m_totalCounts.resource;
        subObjectRange.offset.sampler = m_totalCounts.sampler;

        // Along with the offset information, we also need to compute the
        // "stride" between consecutive sub-objects in the range. The actual
        // size/stride of a single object depends on the type of range we
        // are dealing with.
        //
        BindingOffset objectCounts;
        switch (slangBindingType)
        {
        default:
        {
            // We only treat buffers of interface types as actual sub-object binding
            // range.
            auto bindingRangeTypeLayout = typeLayout->getBindingRangeLeafTypeLayout(bindingRangeIndex);
            if (!bindingRangeTypeLayout)
                continue;
            auto elementType = typeLayout->getBindingRangeLeafTypeLayout(bindingRangeIndex)->getElementTypeLayout();
            if (!elementType)
                continue;
            if (elementType->getKind() != slang::TypeReflection::Kind::Interface)
            {
                continue;
            }
        }
        break;

        case slang::BindingType::ConstantBuffer:
        {
            SLANG_RHI_ASSERT(subObjectLayout);

            // The resource and sampler descriptors of a nested
            // constant buffer will "leak" into those of the
            // parent type, and we need to account for them
            // whenever we allocate storage.
            //
            objectCounts.resource = subObjectLayout->getTotalResourceDescriptorCount();
            objectCounts.sampler = subObjectLayout->getTotalSamplerDescriptorCount();
            objectCounts.rootParam = subObjectRange.layout->getChildRootParameterCount();
        }
        break;

        case slang::BindingType::ParameterBlock:
        {
            SLANG_RHI_ASSERT(subObjectLayout);

            // In contrast to a constant buffer, a parameter block can hide
            // the resource and sampler descriptor allocation it uses (since they
            // are allocated into the tables that make up the parameter block.
            //
            // The only resource usage that leaks into the surrounding context
            // is the number of root parameters consumed.
            //
            objectCounts.rootParam = subObjectRange.layout->getTotalRootTableParameterCount();
        }
        break;

        case slang::BindingType::ExistentialValue:
            // An unspecialized existential/interface value cannot consume any resources
            // as part of the parent object (it needs to fit inside the fixed-size
            // represnetation of existential types).
            //
            // However, if we are statically specializing to a type that doesn't "fit"
            // we may need to account for additional information that needs to be
            // allocaated.
            //
            // Pending data layout APIs have been removed.
            // Interface-type ranges now have no additional resource requirements.
            // The subObjectLayout will be nullptr for interface types.
            break;
        }

        // Once we've computed the usage for each object in the range, we can
        // easily compute the usage for the entire range.
        //
        auto rangeResourceCount = count * objectCounts.resource;
        auto rangeSamplerCount = count * objectCounts.sampler;
        auto rangeRootParamCount = count * objectCounts.rootParam;

        m_totalCounts.resource += rangeResourceCount;
        m_totalCounts.sampler += rangeSamplerCount;
        m_childRootParameterCount += rangeRootParamCount;


        m_subObjectRanges.push_back(subObjectRange);
    }

    // Once we have added up the resource usage from all the sub-objects
    // we can look at the total number of resources and samplers that
    // need to be bound as part of this objects descriptor tables and
    // that will allow us to decide whether we need to allocate a root
    // parameter for a resource table or not, ans similarly for a
    // sampler table.
    //
    if (m_totalCounts.resource)
        m_ownCounts.rootParam++;
    if (m_totalCounts.sampler)
        m_ownCounts.rootParam++;

    m_totalCounts.rootParam = m_ownCounts.rootParam + m_childRootParameterCount;

    return SLANG_OK;
}

Result ShaderObjectLayoutImpl::Builder::build(ShaderObjectLayoutImpl** outLayout)
{
    auto layout = RefPtr<ShaderObjectLayoutImpl>(new ShaderObjectLayoutImpl());
    SLANG_RETURN_ON_FAIL(layout->init(this));

    returnRefPtrMove(outLayout, layout);
    return SLANG_OK;
}

Result RootShaderObjectLayoutImpl::Builder::build(RootShaderObjectLayoutImpl** outLayout)
{
    RefPtr<RootShaderObjectLayoutImpl> layout = new RootShaderObjectLayoutImpl();
    SLANG_RETURN_ON_FAIL(layout->init(this));

    returnRefPtrMove(outLayout, layout);
    return SLANG_OK;
}

void RootShaderObjectLayoutImpl::Builder::addGlobalParams(slang::VariableLayoutReflection* globalsLayout)
{
    setElementTypeLayout(globalsLayout->getTypeLayout());
}

void RootShaderObjectLayoutImpl::Builder::addEntryPoint(SlangStage stage, ShaderObjectLayoutImpl* entryPointLayout)
{
    EntryPointInfo info;
    info.layout = entryPointLayout;

    info.offset.resource = m_totalCounts.resource;
    info.offset.sampler = m_totalCounts.sampler;
    info.offset.rootParam = m_childRootParameterCount;

    m_totalCounts.resource += entryPointLayout->getTotalResourceDescriptorCount();
    m_totalCounts.sampler += entryPointLayout->getTotalSamplerDescriptorCount();

    // TODO(shaderobject) is this correct?
    m_totalCounts.rootParam += entryPointLayout->getTotalRootTableParameterCount();

    // TODO(tfoley): Check this to make sure it is reasonable...
    m_childRootParameterCount += entryPointLayout->getChildRootParameterCount();

    m_entryPoints.push_back(info);
}

Result RootShaderObjectLayoutImpl::RootSignatureDescBuilder::translateDescriptorRangeType(
    slang::BindingType c,
    D3D12_DESCRIPTOR_RANGE_TYPE* outType
)
{
    switch (c)
    {
    case slang::BindingType::ConstantBuffer:
        *outType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
        return SLANG_OK;
    case slang::BindingType::RawBuffer:
    case slang::BindingType::Texture:
    case slang::BindingType::TypedBuffer:
    case slang::BindingType::RayTracingAccelerationStructure:
        *outType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        return SLANG_OK;
    case slang::BindingType::MutableRawBuffer:
    case slang::BindingType::MutableTexture:
    case slang::BindingType::MutableTypedBuffer:
        *outType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        return SLANG_OK;
    case slang::BindingType::Sampler:
        *outType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
        return SLANG_OK;
    default:
        return SLANG_FAIL;
    }
}

/// Add a new descriptor set to the layout being computed.
///
/// Note that a "descriptor set" in the layout may amount to
/// zero, one, or two different descriptor *tables* in the
/// final D3D12 root signature. Each descriptor set may
/// contain zero or more view ranges (CBV/SRV/UAV) and zero
/// or more sampler ranges. It maps to a view descriptor table
/// if the number of view ranges is non-zero and to a sampler
/// descriptor table if the number of sampler ranges is non-zero.
///

uint32_t RootShaderObjectLayoutImpl::RootSignatureDescBuilder::addDescriptorSet()
{
    auto result = (uint32_t)m_descriptorSets.size();
    m_descriptorSets.push_back(DescriptorSetLayout{});
    return result;
}

Result RootShaderObjectLayoutImpl::RootSignatureDescBuilder::addDescriptorRange(
    uint32_t physicalDescriptorSetIndex,
    D3D12_DESCRIPTOR_RANGE_TYPE rangeType,
    UINT registerIndex,
    UINT spaceIndex,
    UINT count,
    bool isRootParameter
)
{
    if (isRootParameter)
    {
        D3D12_ROOT_PARAMETER1 rootParam = {};
        switch (rangeType)
        {
        case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:
            rootParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
            break;
        case D3D12_DESCRIPTOR_RANGE_TYPE_UAV:
            rootParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
            break;
        default:
            m_device->handleMessage(
                DebugMessageType::Error,
                DebugMessageSource::Layer,
                "A shader parameter marked as root parameter is neither SRV nor UAV."
            );
            return SLANG_FAIL;
        }
        rootParam.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        rootParam.Descriptor.RegisterSpace = spaceIndex;
        rootParam.Descriptor.ShaderRegister = registerIndex;
        m_rootParameters.push_back(rootParam);
        return SLANG_OK;
    }

    auto& descriptorSet = m_descriptorSets[physicalDescriptorSetIndex];

    D3D12_DESCRIPTOR_RANGE1 range = {};
    range.RangeType = rangeType;
    range.NumDescriptors = count;
    range.BaseShaderRegister = registerIndex;
    range.RegisterSpace = spaceIndex;
    range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    if (range.RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER)
    {
        descriptorSet.m_samplerRanges.push_back(range);
        descriptorSet.m_samplerCount += range.NumDescriptors;
    }
    else
    {
        descriptorSet.m_resourceRanges.push_back(range);
        descriptorSet.m_resourceCount += range.NumDescriptors;
    }

    return SLANG_OK;
}

/// Add one descriptor range as specified in Slang reflection information to the layout.
///
/// The layout information is taken from `typeLayout` for the descriptor
/// range with the given `descriptorRangeIndex` within the logical
/// descriptor set (reflected by Slang) with the given `logicalDescriptorSetIndex`.
///
/// The `physicalDescriptorSetIndex` is the index in the `m_descriptorSets` array of
/// the descriptor set that the range should be added to.
///
/// The `offset` encodes information about space and/or register offsets that
/// should be applied to descrptor ranges.
///
/// This operation can fail if the given descriptor range encodes a range that
/// doesn't map to anything directly supported by D3D12. Higher-level routines
/// will often want to ignore such failures.
///

Result RootShaderObjectLayoutImpl::RootSignatureDescBuilder::addDescriptorRange(
    slang::TypeLayoutReflection* typeLayout,
    uint32_t physicalDescriptorSetIndex,
    const BindingRegisterOffset& containerOffset,
    const BindingRegisterOffset& elementOffset,
    uint32_t logicalDescriptorSetIndex,
    uint32_t descriptorRangeIndex,
    bool isRootParameter
)
{
    auto bindingType = typeLayout->getDescriptorSetDescriptorRangeType(logicalDescriptorSetIndex, descriptorRangeIndex);
    auto count =
        typeLayout->getDescriptorSetDescriptorRangeDescriptorCount(logicalDescriptorSetIndex, descriptorRangeIndex);
    auto index =
        typeLayout->getDescriptorSetDescriptorRangeIndexOffset(logicalDescriptorSetIndex, descriptorRangeIndex);
    auto space = typeLayout->getDescriptorSetSpaceOffset(logicalDescriptorSetIndex);

    D3D12_DESCRIPTOR_RANGE_TYPE rangeType;
    SLANG_RETURN_ON_FAIL(translateDescriptorRangeType(bindingType, &rangeType));

    return addDescriptorRange(
        physicalDescriptorSetIndex,
        rangeType,
        (UINT)index + elementOffset[rangeType],
        (UINT)space + elementOffset.spaceOffset,
        (UINT)count,
        isRootParameter
    );
}

/// Add one binding range to the computed layout.
///
/// The layout information is taken from `typeLayout` for the binding
/// range with the given `bindingRangeIndex`.
///
/// The `physicalDescriptorSetIndex` is the index in the `m_descriptorSets` array of
/// the descriptor set that the range should be added to.
///
/// The `offset` encodes information about space and/or register offsets that
/// should be applied to descrptor ranges.
///
/// Note that a single binding range may encompass zero or more descriptor ranges.
///

void RootShaderObjectLayoutImpl::RootSignatureDescBuilder::addBindingRange(
    slang::TypeLayoutReflection* typeLayout,
    uint32_t physicalDescriptorSetIndex,
    const BindingRegisterOffset& containerOffset,
    const BindingRegisterOffset& elementOffset,
    uint32_t bindingRangeIndex
)
{
    auto logicalDescriptorSetIndex = typeLayout->getBindingRangeDescriptorSetIndex(bindingRangeIndex);
    auto firstDescriptorRangeIndex = typeLayout->getBindingRangeFirstDescriptorRangeIndex(bindingRangeIndex);
    uint32_t descriptorRangeCount = typeLayout->getBindingRangeDescriptorRangeCount(bindingRangeIndex);
    bool isRootParameter = isBindingRangeRootParameter(
        m_device->m_slangContext.globalSession,
        m_device->m_extendedDesc.rootParameterShaderAttributeName,
        typeLayout,
        bindingRangeIndex
    );
    for (uint32_t i = 0; i < descriptorRangeCount; ++i)
    {
        auto descriptorRangeIndex = firstDescriptorRangeIndex + i;

        // Note: we ignore the `Result` returned by `addDescriptorRange()` because we
        // want to silently skip any ranges that represent kinds of bindings that
        // don't actually exist in D3D12.
        //
        addDescriptorRange(
            typeLayout,
            physicalDescriptorSetIndex,
            containerOffset,
            elementOffset,
            logicalDescriptorSetIndex,
            descriptorRangeIndex,
            isRootParameter
        );
    }
}

void RootShaderObjectLayoutImpl::RootSignatureDescBuilder::addAsValue(
    slang::VariableLayoutReflection* varLayout,
    uint32_t physicalDescriptorSetIndex
)
{
    BindingRegisterOffset offset(varLayout);
    auto elementOffset = offset;
    elementOffset.spaceOffset = 0;
    addAsValue(varLayout->getTypeLayout(), physicalDescriptorSetIndex, offset, elementOffset);
}

/// Add binding ranges and parameter blocks to the root signature.
///
/// The layout information is taken from `typeLayout` which should
/// be a layout for either a program or an entry point.
///
/// The `physicalDescriptorSetIndex` is the index in the `m_descriptorSets` array of
/// the descriptor set that binding ranges not belonging to nested
/// parameter blocks should be added to.
///
/// The `offsetForChildrenThatNeedNewSpace` and `offsetForOrdinaryChildren` parameters
/// encode information about space and/or register offsets that should be applied to
/// descrptor ranges. `offsetForChildrenThatNeedNewSpace` will contain a space offset
/// for children that requires a new space, such as a ParameterBlock.
/// `offsetForOrdinaryChildren` contains the space for all direct children that should
/// be placed in.
///

void RootShaderObjectLayoutImpl::RootSignatureDescBuilder::addAsConstantBuffer(
    slang::TypeLayoutReflection* typeLayout,
    uint32_t physicalDescriptorSetIndex,
    BindingRegisterOffset offsetForChildrenThatNeedNewSpace,
    BindingRegisterOffset offsetForOrdinaryChildren
)
{
    if (typeLayout->getSize(SLANG_PARAMETER_CATEGORY_UNIFORM) != 0)
    {
        auto descriptorRangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
        auto& offsetForRangeType = offsetForOrdinaryChildren.offsetForRangeType[descriptorRangeType];
        addDescriptorRange(
            physicalDescriptorSetIndex,
            descriptorRangeType,
            offsetForRangeType,
            offsetForOrdinaryChildren.spaceOffset,
            1,
            false
        );
        offsetForRangeType++;
    }

    addAsValue(typeLayout, physicalDescriptorSetIndex, offsetForChildrenThatNeedNewSpace, offsetForOrdinaryChildren);
}

void RootShaderObjectLayoutImpl::RootSignatureDescBuilder::addAsValue(
    slang::TypeLayoutReflection* typeLayout,
    uint32_t physicalDescriptorSetIndex,
    BindingRegisterOffset inContainerOffset,
    BindingRegisterOffset inElementOffset
)
{
    // Our first task is to add the binding ranges for stuff that is
    // directly contained in `typeLayout` rather than via sub-objects.
    //
    // Our goal is to have the descriptors for directly-contained views/samplers
    // always be contiguous in CPU and GPU memory, so that we can write
    // to them easily with a single operaiton.
    //
    uint32_t bindingRangeCount = typeLayout->getBindingRangeCount();
    for (uint32_t bindingRangeIndex = 0; bindingRangeIndex < bindingRangeCount; bindingRangeIndex++)
    {
        // We will look at the type of each binding range and intentionally
        // skip those that represent sub-objects.
        //
        auto bindingType = typeLayout->getBindingRangeType(bindingRangeIndex);
        switch (bindingType)
        {
        case slang::BindingType::ConstantBuffer:
        case slang::BindingType::ParameterBlock:
        case slang::BindingType::ExistentialValue:
            continue;

        default:
            break;
        }

        // For binding ranges that don't represent sub-objects, we will add
        // all of the descriptor ranges they encompass to the root signature.
        //
        addBindingRange(typeLayout, physicalDescriptorSetIndex, inContainerOffset, inElementOffset, bindingRangeIndex);
    }

    // Next we need to recursively include everything bound via sub-objects
    uint32_t subObjectRangeCount = typeLayout->getSubObjectRangeCount();
    for (uint32_t subObjectRangeIndex = 0; subObjectRangeIndex < subObjectRangeCount; subObjectRangeIndex++)
    {
        auto bindingRangeIndex = typeLayout->getSubObjectRangeBindingRangeIndex(subObjectRangeIndex);
        auto bindingType = typeLayout->getBindingRangeType(bindingRangeIndex);

        auto subObjectTypeLayout = typeLayout->getBindingRangeLeafTypeLayout(bindingRangeIndex);

        BindingRegisterOffset subObjectRangeContainerOffset = inContainerOffset;
        subObjectRangeContainerOffset +=
            BindingRegisterOffset(typeLayout->getSubObjectRangeOffset(subObjectRangeIndex));
        BindingRegisterOffset subObjectRangeElementOffset = inElementOffset;
        subObjectRangeElementOffset += BindingRegisterOffset(typeLayout->getSubObjectRangeOffset(subObjectRangeIndex));
        subObjectRangeElementOffset.spaceOffset = inElementOffset.spaceOffset;

        switch (bindingType)
        {
        case slang::BindingType::ConstantBuffer:
        {
            auto containerVarLayout = subObjectTypeLayout->getContainerVarLayout();
            SLANG_RHI_ASSERT(containerVarLayout);

            auto elementVarLayout = subObjectTypeLayout->getElementVarLayout();
            SLANG_RHI_ASSERT(elementVarLayout);

            auto elementTypeLayout = elementVarLayout->getTypeLayout();
            SLANG_RHI_ASSERT(elementTypeLayout);

            BindingRegisterOffset containerOffset = subObjectRangeContainerOffset;
            containerOffset += BindingRegisterOffset(containerVarLayout);

            BindingRegisterOffset elementOffset = subObjectRangeElementOffset;
            elementOffset += BindingRegisterOffset(elementVarLayout);

            addAsConstantBuffer(elementTypeLayout, physicalDescriptorSetIndex, containerOffset, elementOffset);
            break;
        }
        case slang::BindingType::ParameterBlock:
        {
            auto containerVarLayout = subObjectTypeLayout->getContainerVarLayout();
            SLANG_RHI_ASSERT(containerVarLayout);

            auto elementVarLayout = subObjectTypeLayout->getElementVarLayout();
            SLANG_RHI_ASSERT(elementVarLayout);

            auto elementTypeLayout = elementVarLayout->getTypeLayout();
            SLANG_RHI_ASSERT(elementTypeLayout);

            BindingRegisterOffset subDescriptorSetOffset;
            subDescriptorSetOffset.spaceOffset = subObjectRangeContainerOffset.spaceOffset;

            auto subPhysicalDescriptorSetIndex = addDescriptorSet();

            // We recursively call `addAsConstantBuffer` to actually generate
            // the root signature bindings for children in the parameter block.
            // We must compute `containerOffset`, which include a space offset
            // that any sub ParameterBlocks should start from, and `elementOffset`
            // that encodes the space offset of the current parameter block.
            // The space offset of the current parameter block can be obtained from the
            // `containerVarLayout`, and the space offset of any sub ParameterBlocks
            // are obatined from `elementVarLayout`.
            BindingRegisterOffset offsetForChildrenThatNeedNewSpace = subDescriptorSetOffset;
            offsetForChildrenThatNeedNewSpace += BindingRegisterOffset(elementVarLayout);
            BindingRegisterOffset offsetForOrindaryChildren = subDescriptorSetOffset;
            offsetForOrindaryChildren += BindingRegisterOffset(containerVarLayout);

            addAsConstantBuffer(
                elementTypeLayout,
                subPhysicalDescriptorSetIndex,
                offsetForChildrenThatNeedNewSpace,
                offsetForOrindaryChildren
            );
            break;
        }
        case slang::BindingType::ExistentialValue:
        {
            // Pending data layout APIs have been removed.
            // Interface-type ranges no longer contribute additional binding ranges.
            break;
        }
        default:
            break;
        }
    }
}

D3D12_ROOT_SIGNATURE_DESC1& RootShaderObjectLayoutImpl::RootSignatureDescBuilder::build()
{
    for (uint32_t i = 0; i < m_descriptorSets.size(); i++)
    {
        auto& descriptorSet = m_descriptorSets[i];
        if (descriptorSet.m_resourceRanges.size())
        {
            D3D12_ROOT_PARAMETER1 rootParam = {};
            rootParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            rootParam.DescriptorTable.NumDescriptorRanges = (UINT)descriptorSet.m_resourceRanges.size();
            rootParam.DescriptorTable.pDescriptorRanges = descriptorSet.m_resourceRanges.data();
            m_rootParameters.push_back(rootParam);
        }
        if (descriptorSet.m_samplerRanges.size())
        {
            D3D12_ROOT_PARAMETER1 rootParam = {};
            rootParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            rootParam.DescriptorTable.NumDescriptorRanges = (UINT)descriptorSet.m_samplerRanges.size();
            rootParam.DescriptorTable.pDescriptorRanges = descriptorSet.m_samplerRanges.data();
            m_rootParameters.push_back(rootParam);
        }
    }

    m_rootSignatureDesc.NumParameters = UINT(m_rootParameters.size());
    m_rootSignatureDesc.pParameters = m_rootParameters.data();

    // TODO: static samplers should be reasonably easy to support...
    m_rootSignatureDesc.NumStaticSamplers = 0;
    m_rootSignatureDesc.pStaticSamplers = nullptr;

    // TODO: only set this flag if needed (requires creating root
    // signature at same time as pipeline state...).
    //
    m_rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    return m_rootSignatureDesc;
}

Result RootShaderObjectLayoutImpl::createRootSignatureFromSlang(
    DeviceImpl* device,
    RootShaderObjectLayoutImpl* rootLayout,
    slang::IComponentType* program,
    ID3D12RootSignature** outRootSignature,
    ID3DBlob** outError
)
{
    // We are going to build up the root signature by adding
    // binding/descritpor ranges and nested parameter blocks
    // based on the computed layout information for `program`.
    //
    RootSignatureDescBuilder builder(device);
    auto layout = program->getLayout();

    // The layout information computed by Slang breaks up shader
    // parameters into what we can think of as "logical" descriptor
    // sets based on whether or not parameters have the same `space`.
    //
    // We want to basically ignore that decomposition and generate a
    // single descriptor set to hold all top-level parameters, and only
    // generate distinct descriptor sets when the shader has opted in
    // via explicit parameter blocks.
    //
    // To achieve this goal, we will manually allocate a default descriptor
    // set for root parameters in our signature, and then recursively
    // add all the binding/descriptor ranges implied by the global-scope
    // parameters.
    //
    auto rootDescriptorSetIndex = builder.addDescriptorSet();
    builder.addAsValue(layout->getGlobalParamsVarLayout(), rootDescriptorSetIndex);

    for (SlangUInt i = 0; i < layout->getEntryPointCount(); i++)
    {
        // Entry-point parameters should also be added to the default root
        // descriptor set.
        //
        // We add the parameters using the "variable layout" for the entry point
        // and not just its type layout, to ensure that any offset information is
        // applied correctly to the `register` and `space` information for entry-point
        // parameters.
        //
        // Note: When we start to support DXR we will need to handle entry-point parameters
        // differently because they will need to map to local root signatures rather than
        // being included in the global root signature as is being done here.
        //
        auto entryPoint = layout->getEntryPointByIndex(i);
        builder.addAsValue(entryPoint->getVarLayout(), rootDescriptorSetIndex);
    }

#if SLANG_RHI_ENABLE_NVAPI
    // Create extra descriptor range for NVAPI UAV slot if a range does not yet exist.
    // This happens when the shader does not explicitly include the NVAPI header.
    if (device->m_nvapiShaderExtension)
    {
        const DescriptorSetLayout& rootDescriptorSetLayout = builder.m_descriptorSets[rootDescriptorSetIndex];
        bool foundRange = false;
        for (const auto& range : rootDescriptorSetLayout.m_resourceRanges)
        {
            if (range.BaseShaderRegister == device->m_nvapiShaderExtension.uavSlot &&
                range.RegisterSpace == device->m_nvapiShaderExtension.registerSpace)
            {
                foundRange = true;
            }
        }
        if (!foundRange)
        {
            builder.addDescriptorRange(
                rootDescriptorSetIndex,
                D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
                device->m_nvapiShaderExtension.uavSlot,
                device->m_nvapiShaderExtension.registerSpace,
                1,
                false
            );
            rootLayout->m_totalCounts.resource += 1;
            rootLayout->m_hasImplicitDescriptorRangeForNVAPI = true;
        }
    }
#endif

    // This is hacky, before calling build(), m_rootParameters contains only the root parameters.
    rootLayout->m_rootSignatureRootParameterCount = builder.m_rootParameters.size();
    auto& rootSignatureDesc = builder.build();
    // After build, m_rootParameters also contains the descriptor tables.
    rootLayout->m_rootSignatureTotalParameterCount = builder.m_rootParameters.size();
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC versionedDesc = {};
    versionedDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    versionedDesc.Desc_1_1 = rootSignatureDesc;
    if (builder.m_device->hasFeature(Feature::Bindless))
    {
        versionedDesc.Desc_1_1.Flags |= D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED |
                                        D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED;
    }
    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;
    if (SLANG_FAILED(
            device->m_D3D12SerializeVersionedRootSignature(&versionedDesc, signature.writeRef(), error.writeRef())
        ))
    {
        device->handleMessage(
            DebugMessageType::Error,
            DebugMessageSource::Layer,
            "error: D3D12SerializeRootSignature failed"
        );
        if (error)
        {
            device->handleMessage(
                DebugMessageType::Error,
                DebugMessageSource::Driver,
                (const char*)error->GetBufferPointer()
            );
            if (outError)
                returnComPtr(outError, error);
        }
        return SLANG_FAIL;
    }

    SLANG_RETURN_ON_FAIL(device->m_device->CreateRootSignature(
        0,
        signature->GetBufferPointer(),
        signature->GetBufferSize(),
        IID_PPV_ARGS(outRootSignature)
    ));
    return SLANG_OK;
}

Result RootShaderObjectLayoutImpl::create(
    DeviceImpl* device,
    slang::IComponentType* program,
    slang::ProgramLayout* programLayout,
    RootShaderObjectLayoutImpl** outLayout,
    ID3DBlob** outError
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
        builder.addEntryPoint(slangEntryPoint->getStage(), entryPointLayout);
    }

    RefPtr<RootShaderObjectLayoutImpl> layout;
    SLANG_RETURN_ON_FAIL(builder.build(layout.writeRef()));

    if (program->getSpecializationParamCount() == 0)
    {
        // For root object, we would like know the union of all binding slots
        // including all sub-objects in the shader-object hierarchy, so at
        // parameter binding time we can easily know how many GPU descriptor tables
        // to create without walking through the shader-object hierarchy again.
        // We build out this array along with root signature construction and store
        // it in `m_gpuDescriptorSetInfos`.
        SLANG_RETURN_ON_FAIL(
            createRootSignatureFromSlang(device, layout, program, layout->m_rootSignature.writeRef(), outError)
        );
    }

    *outLayout = layout.detach();

    return SLANG_OK;
}

Result RootShaderObjectLayoutImpl::init(Builder* builder)
{
    SLANG_RETURN_ON_FAIL(Super::init(builder));

    m_program = builder->m_program;
    m_programLayout = builder->m_programLayout;
    m_entryPoints = builder->m_entryPoints;
    return SLANG_OK;
}

} // namespace rhi::d3d12
