#include "shader-object.h"

#include "rhi-shared.h"

namespace rhi {


// ----------------------------------------------------------------------------
// ShaderObjectLayout
// ----------------------------------------------------------------------------

void ShaderObjectLayout::initBase(
    Device* device,
    slang::ISession* session,
    slang::TypeLayoutReflection* elementTypeLayout
)
{
    m_device = device;
    m_slangSession = session;
    m_elementTypeLayout = elementTypeLayout;
    m_componentID = m_device->m_shaderCache.getComponentId(m_elementTypeLayout->getType());
}

// ----------------------------------------------------------------------------
// ShaderObject
// ----------------------------------------------------------------------------

IShaderObject* ShaderObject::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IShaderObject::getTypeGuid())
        return static_cast<IShaderObject*>(this);
    return nullptr;
}

slang::TypeLayoutReflection* ShaderObject::getElementTypeLayout()
{
    return m_layout->getElementTypeLayout();
}

ShaderObjectContainerType ShaderObject::getContainerType()
{
    return m_layout->getContainerType();
}

uint32_t ShaderObject::getEntryPointCount()
{
    return 0;
}

Result ShaderObject::getEntryPoint(uint32_t index, IShaderObject** outEntryPoint)
{
    *outEntryPoint = nullptr;
    return SLANG_OK;
}

Result ShaderObject::setData(const ShaderOffset& offset, const void* data, Size size)
{
    SLANG_RETURN_ON_FAIL(checkFinalized());

    size_t dataOffset = offset.uniformOffset;
    size_t dataSize = size;

    uint8_t* dest = m_data.data();
    size_t availableSize = m_data.size();

    // TODO(shaderobject): We really should bounds-check access rather than silently ignoring sets
    // that are too large, but we have several test cases that set more data than
    // an object actually stores on several targets...
    //
    if ((dataOffset + dataSize) >= availableSize)
    {
        dataSize = availableSize - dataOffset;
    }

    ::memcpy(dest + dataOffset, data, dataSize);

    incrementVersion();

    return SLANG_OK;
}

Result ShaderObject::getObject(const ShaderOffset& offset, IShaderObject** outObject)
{
    SLANG_RHI_ASSERT(outObject);

    if (offset.bindingRangeIndex >= m_layout->getBindingRangeCount())
        return SLANG_E_INVALID_ARG;
    const auto& bindingRange = m_layout->getBindingRange(offset.bindingRangeIndex);

    returnComPtr(outObject, m_objects[bindingRange.subObjectIndex + offset.bindingArrayIndex]);
    return SLANG_OK;
}

Result ShaderObject::setObject(const ShaderOffset& offset, IShaderObject* object)
{
    if (m_finalized)
        return SLANG_FAIL;

    incrementVersion();

    ShaderObject* subObject = checked_cast<ShaderObject*>(object);
    // There are three different cases in `setObject`.
    // 1. `this` object represents a StructuredBuffer, and `object` is an
    //    element to be written into the StructuredBuffer.
    // 2. `object` represents a StructuredBuffer and we are setting it into
    //    a StructuredBuffer typed field in `this` object.
    // 3. We are setting `object` as an ordinary sub-object, e.g. an existential
    //    field, a constant buffer or a parameter block.
    // We handle each case separately below.

    if (m_layout->getContainerType() != ShaderObjectContainerType::None &&
        m_layout->getContainerType() != ShaderObjectContainerType::ParameterBlock)
    {
        // Case 1:
        // We are setting an element into a `StructuredBuffer` object.
        // We need to hold a reference to the element object, as well as
        // writing uniform data to the plain buffer.
        if (offset.bindingArrayIndex >= m_objects.size())
        {
            m_objects.resize(offset.bindingArrayIndex + 1);
            auto stride = m_layout->getElementTypeLayout()->getStride();
            m_data.resize(m_objects.size() * stride);
        }
        m_objects[offset.bindingArrayIndex] = subObject;

        ExtendedShaderObjectTypeList specializationArgs;

        auto payloadOffset = offset;

        // If the element type of the StructuredBuffer field is an existential type,
        // we need to make sure to fill in the existential value header (RTTI ID and
        // witness table IDs).
        if (m_layout->getElementTypeLayout()->getKind() == slang::TypeReflection::Kind::Interface)
        {
            auto existentialType = m_layout->getElementTypeLayout()->getType();
            ExtendedShaderObjectType concreteType;
            SLANG_RETURN_ON_FAIL(subObject->getSpecializedShaderObjectType(&concreteType));
            SLANG_RETURN_ON_FAIL(setExistentialHeader(existentialType, concreteType.slangType, offset));
            payloadOffset.uniformOffset += 16;
        }
        SLANG_RETURN_ON_FAIL(setData(payloadOffset, subObject->m_data.data(), subObject->m_data.size()));
        return SLANG_OK;
    }

    // Case 2 & 3, setting object as an StructuredBuffer, ConstantBuffer, ParameterBlock or
    // existential value.

    if (offset.bindingRangeIndex >= m_layout->getBindingRangeCount())
        return SLANG_E_INVALID_ARG;

    auto bindingRangeIndex = offset.bindingRangeIndex;
    const auto& bindingRange = m_layout->getBindingRange(bindingRangeIndex);

    m_objects[bindingRange.subObjectIndex + offset.bindingArrayIndex] = subObject;

    switch (bindingRange.bindingType)
    {
    case slang::BindingType::ExistentialValue:
    {
        // If the range being assigned into represents an interface/existential-type
        // leaf field, then we need to consider how the `object` being assigned here
        // affects specialization. We may also need to assign some data from the
        // sub-object into the ordinary data buffer for the parent object.
        //
        // A leaf field of interface type is laid out inside of the parent object
        // as a tuple of `(RTTI, WitnessTable, Payload)`. The layout of these fields
        // is a contract between the compiler and any runtime system, so we will
        // need to rely on details of the binary layout.

        // We start by querying the layout/type of the concrete value that the
        // application is trying to store into the field, and also the layout/type of
        // the leaf existential-type field itself.
        //
        auto concreteTypeLayout = subObject->getElementTypeLayout();
        auto concreteType = concreteTypeLayout->getType();
        //
        auto existentialTypeLayout = m_layout->getElementTypeLayout()->getBindingRangeLeafTypeLayout(bindingRangeIndex);
        auto existentialType = existentialTypeLayout->getType();

        // Fills in the first and second field of the tuple that specify RTTI type ID
        // and witness table ID.
        SLANG_RETURN_ON_FAIL(setExistentialHeader(existentialType, concreteType, offset));

        // The third field of the tuple (offset 16) is the "payload" that is supposed to
        // hold the data for a value of the given concrete type.
        //
        auto payloadOffset = offset;
        payloadOffset.uniformOffset += 16;

        // There are two cases we need to consider here for how the payload might be
        // used:
        //
        // * If the concrete type of the value being bound is one that can "fit" into
        // the
        //   available payload space,  then it should be stored in the payload.
        //
        // * If the concrete type of the value cannot fit in the payload space, then it
        //   will need to be stored somewhere else.
        //
        if (_doesValueFitInExistentialPayload(concreteTypeLayout, existentialTypeLayout))
        {
            // If the value can fit in the payload area, then we will go ahead and copy
            // its bytes into that area.
            //
            setData(payloadOffset, subObject->m_data.data(), subObject->m_data.size());
        }
        else
        {
            // If the value does *not *fit in the payload area, then there is nothing
            // we can do at this point (beyond saving a reference to the sub-object,
            // which was handled above).
            //
            // Once all the sub-objects have been set into the parent object, we can
            // compute a specialized layout for it, and that specialized layout can tell
            // us where the data for these sub-objects has been laid out.
            return SLANG_E_NOT_IMPLEMENTED;
        }
        break;
    }
    case slang::BindingType::MutableRawBuffer:
    case slang::BindingType::RawBuffer:
    {
        // TODO(shaderobject) this is a temporary solution to allow slang render test to work
        // some tests use ShaderObject to build polymorphic structured buffers
        // we should consider this use case and provide a better solution
        // this implementation also doesn't handle the case where CPU/CUDA backends are allowed
        // to place resources into plain structured buffers (because on these backends they are just pointers)
        ComPtr<IBuffer> buffer;
        SLANG_RETURN_ON_FAIL(
            subObject->writeStructuredBuffer(subObject->getElementTypeLayout(), m_layout, buffer.writeRef())
        );
        SLANG_RETURN_ON_FAIL(setBinding(offset, buffer));
        break;
    }
    default:
        break;
    }
    return SLANG_OK;
}

Result ShaderObject::setBinding(const ShaderOffset& offset, const Binding& binding)
{
    SLANG_RETURN_ON_FAIL(checkFinalized());

    auto bindingRangeIndex = offset.bindingRangeIndex;
    if (bindingRangeIndex >= m_layout->getBindingRangeCount())
        return SLANG_E_INVALID_ARG;
    const auto& bindingRange = m_layout->getBindingRange(bindingRangeIndex);
    auto slotIndex = bindingRange.slotIndex + offset.bindingArrayIndex;
    if (slotIndex >= m_slots.size())
        return SLANG_E_INVALID_ARG;

    ResourceSlot& slot = m_slots[slotIndex];

    switch (binding.type)
    {
    case BindingType::Buffer:
    case BindingType::BufferWithCounter:
    {
        Buffer* buffer = checked_cast<Buffer*>(binding.resource.get());
        if (buffer)
        {
            slot.type = BindingType::Buffer;
            slot.resource = buffer;
            if (binding.type == BindingType::BufferWithCounter)
                slot.resource2 = checked_cast<Buffer*>(binding.resource2.get());
            slot.format = buffer->m_desc.format;
            slot.bufferRange = buffer->resolveBufferRange(binding.bufferRange);
        }
        else
        {
            slot = {};
        }
        break;
    }
    case BindingType::Texture:
    {
        TextureView* textureView = checked_cast<TextureView*>(binding.resource.get());
        if (textureView)
        {
            slot.type = BindingType::Texture;
            slot.resource = textureView;
        }
        else
        {
            slot = {};
        }
        break;
    }
    case BindingType::Sampler:
    {
        Sampler* sampler = checked_cast<Sampler*>(binding.resource.get());
        if (sampler)
        {
            slot.type = BindingType::Sampler;
            slot.resource = sampler;
        }
        else
        {
            slot = {};
        }
        break;
    }
    case BindingType::AccelerationStructure:
    {
        AccelerationStructure* accelerationStructure = checked_cast<AccelerationStructure*>(binding.resource.get());
        if (accelerationStructure)
        {
            slot.type = BindingType::AccelerationStructure;
            slot.resource = accelerationStructure;
        }
        else
        {
            slot = {};
        }
        break;
    }
    case BindingType::CombinedTextureSampler:
    {
        TextureView* textureView = checked_cast<TextureView*>(binding.resource.get());
        Sampler* sampler = checked_cast<Sampler*>(binding.resource2.get());
        if (textureView && sampler)
        {
            slot.type = BindingType::CombinedTextureSampler;
            slot.resource = textureView;
            slot.resource2 = sampler;
        }
        else
        {
            slot = {};
        }
        break;
    }
    default:
        return SLANG_E_INVALID_ARG;
    }

    if (m_setBindingHook)
        m_setBindingHook(this, offset, slot, bindingRange.bindingType);

    incrementVersion();

    return SLANG_OK;
}

Result ShaderObject::setDescriptorHandle(const ShaderOffset& offset, const DescriptorHandle& handle)
{
    SLANG_RETURN_ON_FAIL(checkFinalized());

    if (offset.uniformOffset + 8 > m_data.size())
    {
        return SLANG_E_INVALID_ARG;
    }

    ::memcpy(m_data.data() + offset.uniformOffset, &handle.value, 8);

    incrementVersion();

    return SLANG_OK;
}

Result ShaderObject::setSpecializationArgs(
    const ShaderOffset& offset,
    const slang::SpecializationArg* args,
    uint32_t count
)
{
    // If the shader object is a container, delegate the processing to
    // `setSpecializationArgsForContainerElements`.
    if (m_layout->getContainerType() != ShaderObjectContainerType::None)
    {
        ExtendedShaderObjectTypeList argList;
        SLANG_RETURN_ON_FAIL(getExtendedShaderTypeListFromSpecializationArgs(argList, args, count));
        setSpecializationArgsForContainerElement(argList);
        return SLANG_OK;
    }

    if (offset.bindingRangeIndex >= m_layout->getBindingRangeCount())
        return SLANG_E_INVALID_ARG;

    auto bindingRangeIndex = offset.bindingRangeIndex;
    const auto& bindingRange = m_layout->getBindingRange(bindingRangeIndex);
    uint32_t objectIndex = bindingRange.subObjectIndex + offset.bindingArrayIndex;
    if (objectIndex >= m_userProvidedSpecializationArgs.size())
        m_userProvidedSpecializationArgs.resize(objectIndex + 1);
    if (!m_userProvidedSpecializationArgs[objectIndex])
    {
        m_userProvidedSpecializationArgs[objectIndex] = new ExtendedShaderObjectTypeListObject();
    }
    else
    {
        m_userProvidedSpecializationArgs[objectIndex]->clear();
    }
    SLANG_RETURN_ON_FAIL(
        getExtendedShaderTypeListFromSpecializationArgs(*m_userProvidedSpecializationArgs[objectIndex], args, count)
    );
    return SLANG_OK;
}

const void* ShaderObject::getRawData()
{
    return m_data.data();
}

Size ShaderObject::getSize()
{
    return m_data.size();
}

Result ShaderObject::setConstantBufferOverride(IBuffer* outBuffer)
{
    return SLANG_E_NOT_AVAILABLE;
}

Result ShaderObject::finalize()
{
    if (m_finalized)
        return SLANG_FAIL;

    for (auto& object : m_objects)
    {
        if (object && !object->isFinalized())
            SLANG_RETURN_ON_FAIL(object->finalize());
    }

    return SLANG_OK;
}

bool ShaderObject::isFinalized()
{
    return m_finalized;
}

Result ShaderObject::create(Device* device, ShaderObjectLayout* layout, ShaderObject** outShaderObject)
{
    RefPtr<ShaderObject> shaderObject = new ShaderObject();
    SLANG_RETURN_ON_FAIL(shaderObject->init(device, layout));
    returnRefPtr(outShaderObject, shaderObject);
    return SLANG_OK;
}

Result ShaderObject::init(Device* device, ShaderObjectLayout* layout)
{
    m_device = device;
    m_layout = layout;

    // If the layout tells us that there is any uniform data,
    // then we will allocate a CPU memory buffer to hold that data
    // while it is being set from the host.
    //
    // Once the user is done setting the parameters/fields of this
    // shader object, we will produce a GPU-memory version of the
    // uniform data (which includes values from this object and
    // any existential-type sub-objects).
    //
    size_t uniformSize = 0;
    if (layout->getContainerType() == ShaderObjectContainerType::ParameterBlock)
    {
        auto parameterBlockTypeLayout = layout->getParameterBlockTypeLayout();
        uniformSize = parameterBlockTypeLayout->getSize();
    }
    else
    {
        uniformSize = layout->getElementTypeLayout()->getSize();
    }

    if (uniformSize)
    {
        m_data.resize(uniformSize);
        ::memset(m_data.data(), 0, uniformSize);
    }

    m_slots.resize(layout->getSlotCount());

    // If the layout specifies that we have any sub-objects, then
    // we need to size the array to account for them.
    //
    uint32_t subObjectCount = layout->getSubObjectCount();
    m_objects.resize(subObjectCount);

    for (uint32_t subObjectRangeIndex = 0; subObjectRangeIndex < layout->getSubObjectRangeCount();
         ++subObjectRangeIndex)
    {
        const auto& subObjectRange = layout->getSubObjectRange(subObjectRangeIndex);
        auto subObjectLayout = layout->getSubObjectRangeLayout(subObjectRangeIndex);

        // In the case where the sub-object range represents an
        // existential-type leaf field (e.g., an `IBar`), we
        // cannot pre-allocate the object(s) to go into that
        // range, since we can't possibly know what to allocate
        // at this point.
        //
        if (!subObjectLayout)
            continue;
        //
        // Otherwise, we will allocate a sub-object to fill
        // in each entry in this range, based on the layout
        // information we already have.

        const auto& bindingRange = layout->getBindingRange(subObjectRange.bindingRangeIndex);
        for (uint32_t i = 0; i < bindingRange.count; ++i)
        {
            RefPtr<ShaderObject> subObject;
            SLANG_RETURN_ON_FAIL(ShaderObject::create(device, subObjectLayout, subObject.writeRef()));
            m_objects[bindingRange.subObjectIndex + i] = subObject;
        }
    }

    device->customizeShaderObject(this);

    return SLANG_OK;
}

Result ShaderObject::collectSpecializationArgs(ExtendedShaderObjectTypeList& args)
{
    if (m_layout->getContainerType() != ShaderObjectContainerType::None)
    {
        args.addRange(m_structuredBufferSpecializationArgs);
        return SLANG_OK;
    }

    // The following logic is built on the assumption that all fields that involve
    // existential types (and therefore require specialization) will results in a sub-object
    // range in the type layout. This allows us to simply scan the sub-object ranges to find
    // out all specialization arguments.
    uint32_t subObjectRangeCount = m_layout->getSubObjectRangeCount();
    for (uint32_t subObjectRangeIndex = 0; subObjectRangeIndex < subObjectRangeCount; subObjectRangeIndex++)
    {
        const auto& subObjectRange = m_layout->getSubObjectRange(subObjectRangeIndex);
        const auto& bindingRange = m_layout->getBindingRange(subObjectRange.bindingRangeIndex);

        uint32_t oldArgsCount = args.getCount();

        uint32_t count = bindingRange.count;

        for (uint32_t subObjectIndexInRange = 0; subObjectIndexInRange < count; subObjectIndexInRange++)
        {
            ExtendedShaderObjectTypeList typeArgs;
            uint32_t objectIndex = bindingRange.subObjectIndex + subObjectIndexInRange;
            auto subObject = m_objects[objectIndex];

            if (!subObject)
                continue;

            if (objectIndex < m_userProvidedSpecializationArgs.size() && m_userProvidedSpecializationArgs[objectIndex])
            {
                args.addRange(*m_userProvidedSpecializationArgs[objectIndex]);
                continue;
            }

            switch (bindingRange.bindingType)
            {
            case slang::BindingType::ExistentialValue:
            {
                // A binding type of `ExistentialValue` means the sub-object represents a
                // interface-typed field. In this case the specialization argument for this
                // field is the actual specialized type of the bound shader object. If the
                // shader object's type is an ordinary type without existential fields, then
                // the type argument will simply be the ordinary type. But if the sub
                // object's type is itself a specialized type, we need to make sure to use
                // that type as the specialization argument.

                ExtendedShaderObjectType specializedSubObjType;
                SLANG_RETURN_ON_FAIL(subObject->getSpecializedShaderObjectType(&specializedSubObjType));
                typeArgs.add(specializedSubObjType);
                break;
            }
            case slang::BindingType::ParameterBlock:
            case slang::BindingType::ConstantBuffer:
                // If the field's type is `ParameterBlock<IFoo>`, we want to pull in the type argument
                // from the sub object for specialization.
                if (bindingRange.isSpecializable)
                {
                    ExtendedShaderObjectType specializedSubObjType;
                    SLANG_RETURN_ON_FAIL(subObject->getSpecializedShaderObjectType(&specializedSubObjType));
                    typeArgs.add(specializedSubObjType);
                }

                // If field's type is `ParameterBlock<SomeStruct>` or `ConstantBuffer<SomeStruct>`, where
                // `SomeStruct` is a struct type (not directly an interface type), we need to recursively
                // collect the specialization arguments from the bound sub object.
                SLANG_RETURN_ON_FAIL(subObject->collectSpecializationArgs(typeArgs));
                break;
            default:
                break;
            }

            auto addedTypeArgCountForCurrentRange = args.getCount() - oldArgsCount;
            if (addedTypeArgCountForCurrentRange == 0)
            {
                args.addRange(typeArgs);
            }
            else
            {
                // If type arguments for each elements in the array is different, use
                // `__Dynamic` type for the differing argument to disable specialization.
                SLANG_RHI_ASSERT(addedTypeArgCountForCurrentRange == typeArgs.getCount());
                for (uint32_t i = 0; i < addedTypeArgCountForCurrentRange; i++)
                {
                    if (args[i + oldArgsCount].componentID != typeArgs[i].componentID)
                    {
                        auto dynamicType = m_device->m_slangContext.session->getDynamicType();
                        args.componentIDs[i + oldArgsCount] = m_device->m_shaderCache.getComponentId(dynamicType);
                        args.components[i + oldArgsCount] = slang::SpecializationArg::fromType(dynamicType);
                    }
                }
            }
        }
    }
    return SLANG_OK;
}

Result ShaderObject::writeOrdinaryData(void* destData, Size destSize, ShaderObjectLayout* specializedLayout)
{
    SLANG_RHI_ASSERT(m_data.size() <= destSize);
    std::memcpy(destData, m_data.data(), m_data.size());
    return SLANG_OK;
}

Result ShaderObject::writeStructuredBuffer(
    slang::TypeLayoutReflection* elementLayout,
    ShaderObjectLayout* specializedLayout,
    IBuffer** buffer
)
{
    BufferDesc bufferDesc = {};
    bufferDesc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess;
    bufferDesc.defaultState = ResourceState::ShaderResource;
    bufferDesc.size = m_data.size();
    bufferDesc.elementSize = elementLayout->getSize();
    SLANG_RETURN_ON_FAIL(m_device->createBuffer(bufferDesc, m_data.data(), buffer));
    return SLANG_OK;
}

void ShaderObject::trackResources(std::set<RefPtr<RefObject>>& resources)
{
    for (const auto& slot : m_slots)
    {
        if (slot.resource)
            resources.insert(slot.resource);
        if (slot.resource2)
            resources.insert(slot.resource2);
    }
    for (const auto& object : m_objects)
    {
        if (object)
            object->trackResources(resources);
    }
}

Result ShaderObject::getSpecializedShaderObjectType(ExtendedShaderObjectType* outType)
{
    if (m_shaderObjectType.slangType)
        *outType = m_shaderObjectType;
    ExtendedShaderObjectTypeList specializationArgs;
    SLANG_RETURN_ON_FAIL(collectSpecializationArgs(specializationArgs));
    if (specializationArgs.getCount() == 0)
    {
        m_shaderObjectType.componentID = m_layout->getComponentID();
        m_shaderObjectType.slangType = m_layout->getElementTypeLayout()->getType();
    }
    else
    {
        m_shaderObjectType.slangType = m_device->m_slangContext.session->specializeType(
            _getElementTypeLayout()->getType(),
            specializationArgs.components.data(),
            specializationArgs.getCount()
        );
        m_shaderObjectType.componentID = m_device->m_shaderCache.getComponentId(m_shaderObjectType.slangType);
    }
    *outType = m_shaderObjectType;
    return SLANG_OK;
}

Result ShaderObject::getExtendedShaderTypeListFromSpecializationArgs(
    ExtendedShaderObjectTypeList& list,
    const slang::SpecializationArg* args,
    uint32_t count
)
{
    for (uint32_t i = 0; i < count; i++)
    {
        ExtendedShaderObjectType extendedType;
        switch (args[i].kind)
        {
        case slang::SpecializationArg::Kind::Type:
            extendedType.slangType = args[i].type;
            extendedType.componentID = m_device->m_shaderCache.getComponentId(args[i].type);
            break;
        default:
            SLANG_RHI_ASSERT_FAILURE("Unexpected specialization argument kind.");
            return SLANG_FAIL;
        }
        list.add(extendedType);
    }
    return SLANG_OK;
}

void ShaderObject::setSpecializationArgsForContainerElement(ExtendedShaderObjectTypeList& specializationArgs)
{
    // Compute specialization args for the structured buffer object.
    // If we haven't filled anything to `m_structuredBufferSpecializationArgs` yet,
    // use `specializationArgs` directly.
    if (m_structuredBufferSpecializationArgs.getCount() == 0)
    {
        m_structuredBufferSpecializationArgs = _Move(specializationArgs);
    }
    else
    {
        // If `m_structuredBufferSpecializationArgs` already contains some arguments, we
        // need to check if they are the same as `specializationArgs`, and replace
        // anything that is different with `__Dynamic` because we cannot specialize the
        // buffer type if the element types are not the same.
        SLANG_RHI_ASSERT(m_structuredBufferSpecializationArgs.getCount() == specializationArgs.getCount());
        for (uint32_t i = 0; i < m_structuredBufferSpecializationArgs.getCount(); i++)
        {
            if (m_structuredBufferSpecializationArgs[i].componentID != specializationArgs[i].componentID)
            {
                auto dynamicType = m_device->m_slangContext.session->getDynamicType();
                m_structuredBufferSpecializationArgs.componentIDs[i] =
                    m_device->m_shaderCache.getComponentId(dynamicType);
                m_structuredBufferSpecializationArgs.components[i] = slang::SpecializationArg::fromType(dynamicType);
            }
        }
    }
}

Result ShaderObject::setExistentialHeader(
    slang::TypeReflection* existentialType,
    slang::TypeReflection* concreteType,
    ShaderOffset offset
)
{
    // The first field of the tuple (offset zero) is the run-time type information
    // (RTTI) ID for the concrete type being stored into the field.
    //
    // TODO: We need to be able to gather the RTTI type ID from `object` and then
    // use `setData(offset, &TypeID, sizeof(TypeID))`.

    // The second field of the tuple (offset 8) is the ID of the "witness" for the
    // conformance of the concrete type to the interface used by this field.
    //
    auto witnessTableOffset = offset;
    witnessTableOffset.uniformOffset += 8;
    //
    // Conformances of a type to an interface are computed and then stored by the
    // Slang runtime, so we can look up the ID for this particular conformance (which
    // will create it on demand).
    //
    // Note: If the type doesn't actually conform to the required interface for
    // this sub-object range, then this is the point where we will detect that
    // fact and error out.
    //
    uint32_t conformanceID = 0xFFFFFFFF;
    SLANG_RETURN_ON_FAIL(
        m_layout->m_slangSession->getTypeConformanceWitnessSequentialID(concreteType, existentialType, &conformanceID)
    );
    //
    // Once we have the conformance ID, then we can write it into the object
    // at the required offset.
    //
    SLANG_RETURN_ON_FAIL(setData(witnessTableOffset, &conformanceID, sizeof(conformanceID)));

    return SLANG_OK;
}

// ----------------------------------------------------------------------------
// RootShaderObject
// ----------------------------------------------------------------------------

uint32_t RootShaderObject::getEntryPointCount()
{
    return m_entryPoints.size();
}

Result RootShaderObject::getEntryPoint(uint32_t index, IShaderObject** outEntryPoint)
{
    if (index >= m_entryPoints.size())
        return SLANG_E_INVALID_ARG;
    returnComPtr(outEntryPoint, m_entryPoints[index]);
    return SLANG_OK;
}

Result RootShaderObject::create(Device* device, ShaderProgram* program, RootShaderObject** outRootShaderObject)
{
    RefPtr<RootShaderObject> rootShaderObject = new RootShaderObject();
    SLANG_RETURN_ON_FAIL(rootShaderObject->init(device, program));
    returnRefPtr(outRootShaderObject, rootShaderObject);
    return SLANG_OK;
}

Result RootShaderObject::init(Device* device, ShaderProgram* program)
{
    ShaderObjectLayout* layout = program->getRootShaderObjectLayout();
    SLANG_RETURN_ON_FAIL(ShaderObject::init(device, layout));
    m_shaderProgram = program;
    for (uint32_t entryPointIndex = 0; entryPointIndex < layout->getEntryPointCount(); entryPointIndex++)
    {
        ShaderObjectLayout* entryPointLayout = layout->getEntryPointLayout(entryPointIndex);
        RefPtr<ShaderObject> entryPoint;
        SLANG_RETURN_ON_FAIL(ShaderObject::create(device, entryPointLayout, entryPoint.writeRef()));
        m_entryPoints.push_back(entryPoint);
    }
    return SLANG_OK;
}

bool RootShaderObject::isSpecializable() const
{
    return m_shaderProgram->isSpecializable();
}

Result RootShaderObject::getSpecializedLayout(
    const ExtendedShaderObjectTypeList& args,
    ShaderObjectLayout*& outSpecializedLayout
)
{
    outSpecializedLayout = m_shaderProgram->getRootShaderObjectLayout();
    if (m_shaderProgram->isSpecializable() && args.getCount() > 0)
    {
        RefPtr<ShaderProgram> specializedProgram;
        SLANG_RETURN_ON_FAIL(m_device->getSpecializedProgram(m_shaderProgram, args, specializedProgram.writeRef()));
        outSpecializedLayout = specializedProgram->getRootShaderObjectLayout();
    }
    return SLANG_OK;
}

Result RootShaderObject::getSpecializedLayout(ShaderObjectLayout*& outSpecializedLayout)
{
    // Note: There is an important policy decision being made here that we need
    // to approach carefully.
    //
    // We are doing two different things that affect the layout of a program:
    //
    // 1. We are *composing* one or more pieces of code (notably the shared global/module
    //    stuff and the per-entry-point stuff).
    //
    // 2. We are *specializing* code that includes generic/existential parameters
    //    to concrete types/values.
    //
    // We need to decide the relative *order* of these two steps, because of how it impacts
    // layout. The layout for `specialize(compose(A,B), X, Y)` is potentially different
    // form that of `compose(specialize(A,X), specialize(B,Y))`, even when both are
    // semantically equivalent programs.
    //
    // Right now we are using the first option: we are first generating a full composition
    // of all the code we plan to use (global scope plus all entry points), and then
    // specializing it to the concatenated specialization arguments for all of that.
    //
    // In some cases, though, this model isn't appropriate. For example, when dealing with
    // ray-tracing shaders and local root signatures, we really want the parameters of each
    // entry point (actually, each entry-point *group*) to be allocated distinct storage,
    // which really means we want to compute something like:
    //
    //      SpecializedGlobals = specialize(compose(ModuleA, ModuleB, ...), X, Y, ...)
    //
    //      SpecializedEP1 = compose(SpecializedGlobals, specialize(EntryPoint1, T, U, ...))
    //      SpecializedEP2 = compose(SpecializedGlobals, specialize(EntryPoint2, A, B, ...))
    //
    // Note how in this case all entry points agree on the layout for the shared/common
    // parameters, but their layouts are also independent of one another.
    //
    // Furthermore, in this example, loading another entry point into the system would not
    // require re-computing the layouts (or generated kernel code) for any of the entry points
    // that had already been loaded (in contrast to a compose-then-specialize approach).
    //

    outSpecializedLayout = m_shaderProgram->getRootShaderObjectLayout();
    if (m_shaderProgram->isSpecializable())
    {
        ExtendedShaderObjectTypeList args;
        SLANG_RETURN_ON_FAIL(collectSpecializationArgs(args));
        SLANG_RETURN_ON_FAIL(getSpecializedLayout(args, outSpecializedLayout));
    }
    return SLANG_OK;
}

Result RootShaderObject::collectSpecializationArgs(ExtendedShaderObjectTypeList& args)
{
    SLANG_RETURN_ON_FAIL(ShaderObject::collectSpecializationArgs(args));
    for (auto& entryPoint : m_entryPoints)
    {
        SLANG_RETURN_ON_FAIL(entryPoint->collectSpecializationArgs(args));
    }
    return SLANG_OK;
}

void RootShaderObject::trackResources(std::set<RefPtr<RefObject>>& resources)
{
    ShaderObject::trackResources(resources);
    for (const auto& entryPoint : m_entryPoints)
    {
        if (entryPoint)
            entryPoint->trackResources(resources);
    }
}

bool _doesValueFitInExistentialPayload(
    slang::TypeLayoutReflection* concreteTypeLayout,
    slang::TypeLayoutReflection* existentialTypeLayout
)
{
    // Our task here is to figure out if a value of `concreteTypeLayout`
    // can fit into an existential value using `existentialTypelayout`.

    // We can start by asking how many bytes the concrete type of the object consumes.
    //
    auto concreteValueSize = concreteTypeLayout->getSize();

    // We can also compute how many bytes the existential-type value provides,
    // but we need to remember that the *payload* part of that value comes after
    // the header with RTTI and witness-table IDs, so the payload is 16 bytes
    // smaller than the entire value.
    //
    auto existentialValueSize = existentialTypeLayout->getSize();
    auto existentialPayloadSize = existentialValueSize - 16;

    // If the concrete type consumes more ordinary bytes than we have in the payload,
    // it cannot possibly fit.
    //
    if (concreteValueSize > existentialPayloadSize)
        return false;

    // It is possible that the ordinary bytes of `concreteTypeLayout` can fit
    // in the payload, but that type might also use storage other than ordinary
    // bytes. In that case, the value would *not* fit, because all the non-ordinary
    // data can't fit in the payload at all.
    //
    auto categoryCount = concreteTypeLayout->getCategoryCount();
    for (unsigned int i = 0; i < categoryCount; ++i)
    {
        auto category = concreteTypeLayout->getCategoryByIndex(i);
        switch (category)
        {
        // We want to ignore any ordinary/uniform data usage, since that
        // was already checked above.
        //
        case slang::ParameterCategory::Uniform:
            break;

        // Any other kind of data consumed means the value cannot possibly fit.
        default:
            return false;

            // TODO: Are there any cases of resource usage that need to be ignored here?
            // E.g., if the sub-object contains its own existential-type fields (which
            // get reflected as consuming "existential value" storage) should that be
            // ignored?
        }
    }

    // If we didn't reject the concrete type above for either its ordinary
    // data or some use of non-ordinary data, then it seems like it must fit.
    //
    return true;
}
} // namespace rhi
