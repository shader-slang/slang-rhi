#include "wgpu-shader-object-layout.h"
#include "wgpu-device.h"

namespace rhi::wgpu {

inline WGPUTextureViewDimension getViewDimension(SlangResourceShape shape)
{
    switch (shape)
    {
    case SLANG_TEXTURE_1D:
        return WGPUTextureViewDimension_1D;
    case SLANG_TEXTURE_2D:
        return WGPUTextureViewDimension_2D;
    case SLANG_TEXTURE_2D_ARRAY:
        return WGPUTextureViewDimension_2DArray;
    case SLANG_TEXTURE_CUBE:
        return WGPUTextureViewDimension_Cube;
    case SLANG_TEXTURE_CUBE_ARRAY:
        return WGPUTextureViewDimension_CubeArray;
    case SLANG_TEXTURE_3D:
        return WGPUTextureViewDimension_3D;
    default:
        return WGPUTextureViewDimension_Undefined;
    }
}

inline WGPUTextureSampleType getSampleType(slang::TypeReflection* type)
{
    slang::TypeReflection::ScalarType scalarType = type->getScalarType();
    if (scalarType == slang::TypeReflection::ScalarType::None)
    {
        scalarType = type->getElementType()->getScalarType();
    }
    switch (type->getScalarType())
    {
    case slang::TypeReflection::ScalarType::None:
        return WGPUTextureSampleType_Float;
    case slang::TypeReflection::ScalarType::Void:
    case slang::TypeReflection::ScalarType::Bool:
        return WGPUTextureSampleType_Undefined;
    case slang::TypeReflection::ScalarType::Int8:
    case slang::TypeReflection::ScalarType::Int16:
    case slang::TypeReflection::ScalarType::Int32:
    case slang::TypeReflection::ScalarType::Int64:
        return WGPUTextureSampleType_Sint;
    case slang::TypeReflection::ScalarType::UInt8:
    case slang::TypeReflection::ScalarType::UInt16:
    case slang::TypeReflection::ScalarType::UInt32:
    case slang::TypeReflection::ScalarType::UInt64:
        return WGPUTextureSampleType_Uint;
    case slang::TypeReflection::ScalarType::Float16:
    case slang::TypeReflection::ScalarType::Float32:
    case slang::TypeReflection::ScalarType::Float64:
        return WGPUTextureSampleType_Float;
    }
    return WGPUTextureSampleType_Undefined;
}

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
        uint32_t descriptorSetIndex =
            findOrAddDescriptorSet(offset.bindingSet + typeLayout->getDescriptorSetSpaceOffset(i));
        SLANG_UNUSED(descriptorSetIndex);
    }

    // For actually populating the descriptor sets we prefer to enumerate
    // the binding ranges of the type instead of the descriptor sets.
    //
    SlangInt bindRangeCount = typeLayout->getBindingRangeCount();
    for (SlangInt bindingRangeIndex = 0; bindingRangeIndex < bindRangeCount; ++bindingRangeIndex)
    {
        slang::BindingType bindingRangeType = typeLayout->getBindingRangeType(bindingRangeIndex);
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
        }

        // Given a binding range we are interested in, we will then enumerate
        // its contained descriptor ranges.

        SlangInt descriptorRangeCount = typeLayout->getBindingRangeDescriptorRangeCount(bindingRangeIndex);
        if (descriptorRangeCount == 0)
            continue;
        auto slangDescriptorSetIndex = typeLayout->getBindingRangeDescriptorSetIndex(bindingRangeIndex);
        auto descriptorSetIndex = findOrAddDescriptorSet(
            offset.bindingSet + typeLayout->getDescriptorSetSpaceOffset(slangDescriptorSetIndex)
        );
        auto& descriptorSetInfo = m_descriptorSetBuildInfos[descriptorSetIndex];

        SlangInt firstDescriptorRangeIndex = typeLayout->getBindingRangeFirstDescriptorRangeIndex(bindingRangeIndex);
        for (SlangInt j = 0; j < descriptorRangeCount; ++j)
        {
            SlangInt descriptorRangeIndex = firstDescriptorRangeIndex + j;
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

            WGPUBindGroupLayoutEntry entry = {};
            entry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment | WGPUShaderStage_Compute;
            entry.binding = offset.binding + (uint32_t)typeLayout->getDescriptorSetDescriptorRangeIndexOffset(
                                                 slangDescriptorSetIndex,
                                                 descriptorRangeIndex
                                             );

            slang::TypeReflection* leafType = typeLayout->getBindingRangeLeafTypeLayout(bindingRangeIndex)->getType();

            switch (slangDescriptorType)
            {
            case slang::BindingType::Sampler:
                // TODO: figure out sampler type
                entry.sampler.type = WGPUSamplerBindingType_Filtering;
                break;
            case slang::BindingType::CombinedTextureSampler:
                SLANG_RHI_ASSERT_FAILURE("CombinedTextureSampler is not supported");
                break;
            case slang::BindingType::Texture:
                entry.texture.sampleType = getSampleType(leafType->getResourceResultType());
                entry.texture.viewDimension = getViewDimension(leafType->getResourceShape());
                entry.texture.multisampled = (leafType->getResourceShape() & SLANG_TEXTURE_MULTISAMPLE_FLAG) ? 1 : 0;
                break;
            case slang::BindingType::MutableTexture:
                // WGPUStorageTextureAccess_Undefined = 0x00000000,
                // WGPUStorageTextureAccess_WriteOnly = 0x00000001,
                // WGPUStorageTextureAccess_ReadOnly = 0x00000002,
                // WGPUStorageTextureAccess_ReadWrite = 0x00000003,
                entry.storageTexture.access = WGPUStorageTextureAccess_Undefined;
                entry.storageTexture.format = WGPUTextureFormat_RGBA8Unorm;
                // WGPUTextureFormat format;
                entry.storageTexture.viewDimension = getViewDimension(typeLayout->getType()->getResourceShape());
                break;
            case slang::BindingType::TypedBuffer:
            case slang::BindingType::RawBuffer:
                // entry.visibility = WGPUShaderStage_Fragment | WGPUShaderStage_Compute;
                entry.buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
                break;
            case slang::BindingType::MutableTypedBuffer:
            case slang::BindingType::MutableRawBuffer:
                entry.visibility = WGPUShaderStage_Fragment | WGPUShaderStage_Compute;
                entry.buffer.type = WGPUBufferBindingType_Storage;
                break;
            case slang::BindingType::InputRenderTarget:
                break;
            case slang::BindingType::InlineUniformData:
                break;
            case slang::BindingType::RayTracingAccelerationStructure:
                break;
            case slang::BindingType::ConstantBuffer:
                entry.buffer.type = WGPUBufferBindingType_Uniform;
                break;
            default:
                break;
            }

            descriptorSetInfo.entries.push_back(entry);
        }
    }

    // We skipped over the sub-object ranges when adding descriptors above,
    // and now we will address that oversight by iterating over just
    // the sub-object ranges.
    //
    SlangInt subObjectRangeCount = typeLayout->getSubObjectRangeCount();
    for (SlangInt subObjectRangeIndex = 0; subObjectRangeIndex < subObjectRangeCount; ++subObjectRangeIndex)
    {
        SlangInt bindingRangeIndex = typeLayout->getSubObjectRangeBindingRangeIndex(subObjectRangeIndex);
        slang::BindingType bindingType = typeLayout->getBindingRangeType(bindingRangeIndex);

        slang::TypeLayoutReflection* subObjectTypeLayout = typeLayout->getBindingRangeLeafTypeLayout(bindingRangeIndex);
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
        }
    }
} // namespace rhi::wgpu

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
        WGPUBindGroupLayoutEntry entry = {};
        entry.binding = containerOffset.binding;
        entry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment | WGPUShaderStage_Compute;
        entry.buffer.type = WGPUBufferBindingType_Uniform;
        entry.buffer.hasDynamicOffset = false;
        entry.buffer.minBindingSize = elementTypeLayout->getSize(SLANG_PARAMETER_CATEGORY_UNIFORM);
        descriptorSetInfo.entries.push_back(entry);
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

            SlangInt set = typeLayout->getDescriptorSetSpaceOffset(descriptorSetIndex);
            SlangInt bindingOffset =
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
        slang::BindingType slangBindingType = typeLayout->getBindingRangeType(bindingRangeIndex);
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
            // Interface-type ranges are no longer supported after pending data removal.
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
            break;

        case slang::BindingType::ConstantBuffer:
            m_childDescriptorSetCount += subObjectLayout->getChildDescriptorSetCount();
            m_totalBindingCount += subObjectLayout->getTotalBindingCount();
            break;

        case slang::BindingType::ExistentialValue:
            if (subObjectLayout)
            {
                m_childDescriptorSetCount += subObjectLayout->getChildDescriptorSetCount();
                m_totalBindingCount += subObjectLayout->getTotalBindingCount();
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
        getDevice()->m_ctx.api.wgpuBindGroupLayoutRelease(descSetInfo.bindGroupLayout);
    }
}

Result ShaderObjectLayoutImpl::_init(const Builder* builder)
{
    auto device = builder->m_device;

    initBase(device, builder->m_session, builder->m_elementTypeLayout);

    m_bindingRanges = builder->m_bindingRanges;

    m_descriptorSetInfos = _Move(builder->m_descriptorSetBuildInfos);
    m_slotCount = builder->m_slotCount;
    m_childDescriptorSetCount = builder->m_childDescriptorSetCount;
    m_totalBindingCount = builder->m_totalBindingCount;
    m_subObjectCount = builder->m_subObjectCount;
    m_subObjectRanges = builder->m_subObjectRanges;
    m_totalOrdinaryDataSize = builder->m_totalOrdinaryDataSize;

    m_containerType = builder->m_containerType;

    // Create WGPUBindGroupLayout for all descriptor sets.
    for (auto& descriptorSetInfo : m_descriptorSetInfos)
    {
        WGPUBindGroupLayoutDescriptor bindGroupLayoutDesc = {};
        bindGroupLayoutDesc.entries = descriptorSetInfo.entries.data();
        bindGroupLayoutDesc.entryCount = (uint32_t)descriptorSetInfo.entries.size();
        descriptorSetInfo.bindGroupLayout =
            device->m_ctx.api.wgpuDeviceCreateBindGroupLayout(device->m_ctx.device, &bindGroupLayoutDesc);
        if (!descriptorSetInfo.bindGroupLayout)
        {
            return SLANG_FAIL;
        }
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

    // Note: we do not bother adding any descriptor sets/ranges here,
    // because the descriptor ranges of an entry point will simply
    // be allocated as part of the descriptor sets for the root
    // shader object.
}

Result EntryPointLayout::_init(const Builder* builder)
{
    SLANG_RETURN_ON_FAIL(Super::_init(builder));

    m_slangEntryPointLayout = builder->m_slangEntryPointLayout;
    return SLANG_OK;
}

RootShaderObjectLayoutImpl::~RootShaderObjectLayoutImpl()
{
    if (m_pipelineLayout)
    {
        m_device->m_ctx.api.wgpuPipelineLayoutRelease(m_pipelineLayout);
    }
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

    // Once we've collected the information across the entire
    // tree of sub-objects

    // Now call WGPU API to create a pipeline layout.
    WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
    pipelineLayoutDesc.bindGroupLayouts = m_bindGroupLayouts.data();
    pipelineLayoutDesc.bindGroupLayoutCount = (uint32_t)m_bindGroupLayouts.size();
    m_pipelineLayout = m_device->m_ctx.api.wgpuDeviceCreatePipelineLayout(m_device->m_ctx.device, &pipelineLayoutDesc);
    return m_pipelineLayout ? SLANG_OK : SLANG_FAIL;
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
    for (const auto& entryPoint : m_entryPoints)
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
        m_bindGroupLayouts.push_back(descSetInfo.bindGroupLayout);
    }

    SLANG_RETURN_ON_FAIL(addChildDescriptorSetsRec(layout));
    return SLANG_OK;
}

/// Recurisvely add descriptor sets defined by sub-objects of `layout`

Result RootShaderObjectLayoutImpl::addChildDescriptorSetsRec(ShaderObjectLayoutImpl* layout)
{
    for (const auto& subObject : layout->m_subObjectRanges)
    {
        const auto& bindingRange = layout->m_bindingRanges[subObject.bindingRangeIndex];
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

    // We want to keep track of the offset that was applied to "pending"
    // data because we will need it again later when it comes time to
    // actually bind things.
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
}

} // namespace rhi::wgpu
