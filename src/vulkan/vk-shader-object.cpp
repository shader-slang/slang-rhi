#include "vk-shader-object.h"

#include "../state-tracking.h"

namespace rhi::vk {

Result ShaderObjectImpl::create(DeviceImpl* device, ShaderObjectLayoutImpl* layout, ShaderObjectImpl** outShaderObject)
{
    auto object = RefPtr<ShaderObjectImpl>(new ShaderObjectImpl());
    SLANG_RETURN_ON_FAIL(object->init(device, layout));

    returnRefPtrMove(outShaderObject, object);
    return SLANG_OK;
}

Device* ShaderObjectImpl::getDevice()
{
    return m_layout->getDevice();
}

GfxCount ShaderObjectImpl::getEntryPointCount()
{
    return 0;
}

Result ShaderObjectImpl::getEntryPoint(Index index, IShaderObject** outEntryPoint)
{
    *outEntryPoint = nullptr;
    return SLANG_OK;
}

const void* ShaderObjectImpl::getRawData()
{
    return m_data.getBuffer();
}

Size ShaderObjectImpl::getSize()
{
    return (Size)m_data.getCount();
}

// TODO: Change size_t and Index to Size?
Result ShaderObjectImpl::setData(ShaderOffset const& inOffset, void const* data, size_t inSize)
{
    SLANG_RETURN_ON_FAIL(requireNotFinalized());

    Index offset = inOffset.uniformOffset;
    Index size = inSize;

    uint8_t* dest = m_data.getBuffer();
    Index availableSize = m_data.getCount();

    // TODO: We really should bounds-check access rather than silently ignoring sets
    // that are too large, but we have several test cases that set more data than
    // an object actually stores on several targets...
    //
    if (offset < 0)
    {
        size += offset;
        offset = 0;
    }
    if ((offset + size) >= availableSize)
    {
        size = availableSize - offset;
    }

    memcpy(dest + offset, data, size);

#if 0 // TODO
    m_isConstantBufferDirty = true;
#endif

    return SLANG_OK;
}

Result ShaderObjectImpl::setBinding(ShaderOffset const& offset, Binding binding)
{
    SLANG_RETURN_ON_FAIL(requireNotFinalized());

    if (offset.bindingRangeIndex < 0)
        return SLANG_E_INVALID_ARG;
    auto layout = getLayout();
    if (offset.bindingRangeIndex >= layout->getBindingRangeCount())
        return SLANG_E_INVALID_ARG;
    auto& bindingRange = layout->getBindingRange(offset.bindingRangeIndex);

    Index bindingIndex = bindingRange.baseIndex + offset.bindingArrayIndex;

    ResourceSlot& slot = m_resources[bindingIndex];

    switch (binding.type)
    {
    case BindingType::Buffer:
    {
        BufferImpl* buffer = checked_cast<BufferImpl*>(binding.resource.get());
        slot.type = BindingType::Buffer;
        slot.resource = buffer;
        slot.format = slot.format != Format::Unknown ? slot.format : buffer->m_desc.format;
        slot.bufferRange = buffer->resolveBufferRange(slot.bufferRange);
        switch (bindingRange.bindingType)
        {
        case slang::BindingType::TypedBuffer:
        case slang::BindingType::RawBuffer:
            slot.requiredState = ResourceState::ShaderResource;
            break;
        case slang::BindingType::MutableTypedBuffer:
        case slang::BindingType::MutableRawBuffer:
            slot.requiredState = ResourceState::UnorderedAccess;
            break;
        }
        break;
    }
    case BindingType::Texture:
    {
        TextureImpl* texture = checked_cast<TextureImpl*>(binding.resource.get());
        return setBinding(offset, m_device->createTextureView(texture, {}));
    }
    case BindingType::TextureView:
    {
        slot.type = BindingType::TextureView;
        slot.resource = checked_cast<TextureViewImpl*>(binding.resource.get());
        switch (bindingRange.bindingType)
        {
        case slang::BindingType::Texture:
            slot.requiredState = ResourceState::ShaderResource;
            break;
        case slang::BindingType::MutableTexture:
            slot.requiredState = ResourceState::UnorderedAccess;
            break;
        }
        break;
    }
    case BindingType::Sampler:
        m_samplers[bindingIndex] = checked_cast<SamplerImpl*>(binding.resource.get());
        break;
    case BindingType::CombinedTextureSampler:
    {
        TextureImpl* texture = checked_cast<TextureImpl*>(binding.resource.get());
        m_combinedTextureSamplers[bindingIndex] = CombinedTextureSamplerSlot{
            checked_cast<TextureViewImpl*>(m_device->createTextureView(texture, {}).get()),
            checked_cast<SamplerImpl*>(binding.resource2.get())
        };
        break;
    }
    case BindingType::AccelerationStructure:
        slot.type = BindingType::AccelerationStructure;
        slot.resource = checked_cast<AccelerationStructureImpl*>(binding.resource.get());
        break;
    }

    return SLANG_OK;
}

Result ShaderObjectImpl::init(DeviceImpl* device, ShaderObjectLayoutImpl* layout)
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
    // TODO: Change size_t to Count?
    size_t uniformSize = layout->getElementTypeLayout()->getSize();
    if (uniformSize)
    {
        m_data.setCount(uniformSize);
        memset(m_data.getBuffer(), 0, uniformSize);
    }

#if 0
        // If the layout tells us there are any descriptor sets to
        // allocate, then we do so now.
        //
        for(auto descriptorSetInfo : layout->getDescriptorSets())
        {
            RefPtr<DescriptorSet> descriptorSet;
            SLANG_RETURN_ON_FAIL(device->createDescriptorSet(descriptorSetInfo->layout, descriptorSet.writeRef()));
            m_descriptorSets.add(descriptorSet);
        }
#endif

    m_resources.resize(layout->getResourceCount());
    m_samplers.resize(layout->getSamplerCount());
    m_combinedTextureSamplers.resize(layout->getCombinedTextureSamplerCount());

    // If the layout specifies that we have any sub-objects, then
    // we need to size the array to account for them.
    //
    Index subObjectCount = layout->getSubObjectCount();
    m_objects.resize(subObjectCount);

    for (auto subObjectRangeInfo : layout->getSubObjectRanges())
    {
        auto subObjectLayout = subObjectRangeInfo.layout;

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

        auto& bindingRangeInfo = layout->getBindingRange(subObjectRangeInfo.bindingRangeIndex);
        for (Index i = 0; i < bindingRangeInfo.count; ++i)
        {
            RefPtr<ShaderObjectImpl> subObject;
            SLANG_RETURN_ON_FAIL(ShaderObjectImpl::create(device, subObjectLayout, subObject.writeRef()));
            m_objects[bindingRangeInfo.subObjectIndex + i] = subObject;
        }
    }

    m_state = State::Initialized;

    return SLANG_OK;
}

Result ShaderObjectImpl::_writeOrdinaryData(
    BindingContext& context,
    BufferImpl* buffer,
    Offset offset,
    Size destSize,
    ShaderObjectLayoutImpl* specializedLayout
) const
{
    auto src = m_data.getBuffer();
    // TODO: Change size_t to Count?
    auto srcSize = size_t(m_data.getCount());

    SLANG_RHI_ASSERT(srcSize <= destSize);

    context.writeBuffer(buffer, offset, srcSize, src);

    // In the case where this object has any sub-objects of
    // existential/interface type, we need to recurse on those objects
    // that need to write their state into an appropriate "pending" allocation.
    //
    // Note: Any values that could fit into the "payload" included
    // in the existential-type field itself will have already been
    // written as part of `setObject()`. This loop only needs to handle
    // those sub-objects that do not "fit."
    //
    // An implementers looking at this code might wonder if things could be changed
    // so that *all* writes related to sub-objects for interface-type fields could
    // be handled in this one location, rather than having some in `setObject()` and
    // others handled here.
    //
    Index subObjectRangeCounter = 0;
    for (auto const& subObjectRangeInfo : specializedLayout->getSubObjectRanges())
    {
        Index subObjectRangeIndex = subObjectRangeCounter++;
        auto const& bindingRangeInfo = specializedLayout->getBindingRange(subObjectRangeInfo.bindingRangeIndex);

        // We only need to handle sub-object ranges for interface/existential-type fields,
        // because fields of constant-buffer or parameter-block type are responsible for
        // the ordinary/uniform data of their own existential/interface-type sub-objects.
        //
        if (bindingRangeInfo.bindingType != slang::BindingType::ExistentialValue)
            continue;

        // Each sub-object range represents a single "leaf" field, but might be nested
        // under zero or more outer arrays, such that the number of existential values
        // in the same range can be one or more.
        //
        auto count = bindingRangeInfo.count;

        // We are not concerned with the case where the existential value(s) in the range
        // git into the payload part of the leaf field.
        //
        // In the case where the value didn't fit, the Slang layout strategy would have
        // considered the requirements of the value as a "pending" allocation, and would
        // allocate storage for the ordinary/uniform part of that pending allocation inside
        // of the parent object's type layout.
        //
        // Here we assume that the Slang reflection API can provide us with a single byte
        // offset and stride for the location of the pending data allocation in the
        // specialized type layout, which will store the values for this sub-object range.
        //
        // TODO: The reflection API functions we are assuming here haven't been implemented
        // yet, so the functions being called here are stubs.
        //
        // TODO: It might not be that a single sub-object range can reliably map to a single
        // contiguous array with a single stride; we need to carefully consider what the
        // layout logic does for complex cases with multiple layers of nested arrays and
        // structures.
        //
        Offset subObjectRangePendingDataOffset = subObjectRangeInfo.offset.pendingOrdinaryData;
        Size subObjectRangePendingDataStride = subObjectRangeInfo.stride.pendingOrdinaryData;

        // If the range doesn't actually need/use the "pending" allocation at all, then
        // we need to detect that case and skip such ranges.
        //
        // TODO: This should probably be handled on a per-object basis by caching a "does it
        // fit?" bit as part of the information for bound sub-objects, given that we already
        // compute the "does it fit?" status as part of `setObject()`.
        //
        if (subObjectRangePendingDataOffset == 0)
            continue;

        for (Index i = 0; i < count; ++i)
        {
            auto subObject = m_objects[bindingRangeInfo.subObjectIndex + i];

            RefPtr<ShaderObjectLayoutImpl> subObjectLayout;
            SLANG_RETURN_ON_FAIL(subObject->_getSpecializedLayout(subObjectLayout.writeRef()));

            auto subObjectOffset = subObjectRangePendingDataOffset + i * subObjectRangePendingDataStride;

            subObject->_writeOrdinaryData(
                context,
                buffer,
                offset + subObjectOffset,
                destSize - subObjectOffset,
                subObjectLayout
            );
        }
    }

    return SLANG_OK;
}

void ShaderObjectImpl::writeDescriptor(BindingContext& context, VkWriteDescriptorSet const& write)
{
    auto device = context.device;
    device->m_api.vkUpdateDescriptorSets(device->m_device, 1, &write, 0, nullptr);
}

void ShaderObjectImpl::writeBufferDescriptor(
    BindingContext& context,
    BindingOffset const& offset,
    VkDescriptorType descriptorType,
    BufferImpl* buffer,
    Offset bufferOffset,
    Size bufferSize
)
{
    VkDescriptorSet descriptorSet = context.bindable->descriptorSets[offset.bindingSet];

    VkDescriptorBufferInfo bufferInfo = {};
    if (buffer)
    {
        bufferInfo.buffer = buffer->m_buffer.m_buffer;
    }
    bufferInfo.offset = bufferOffset;
    bufferInfo.range = bufferSize;

    VkWriteDescriptorSet write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.descriptorCount = 1;
    write.descriptorType = descriptorType;
    write.dstArrayElement = 0;
    write.dstBinding = offset.binding;
    write.dstSet = descriptorSet;
    write.pBufferInfo = &bufferInfo;

    writeDescriptor(context, write);
}

void ShaderObjectImpl::writeBufferDescriptor(
    BindingContext& context,
    BindingOffset const& offset,
    VkDescriptorType descriptorType,
    BufferImpl* buffer
)
{
    writeBufferDescriptor(context, offset, descriptorType, buffer, 0, buffer->m_desc.size);
}

void ShaderObjectImpl::writePlainBufferDescriptor(
    BindingContext& context,
    BindingOffset const& offset,
    VkDescriptorType descriptorType,
    span<const ResourceSlot> slots
)
{
    VkDescriptorSet descriptorSet = context.bindable->descriptorSets[offset.bindingSet];

    Index count = slots.size();
    for (Index i = 0; i < count; ++i)
    {
        const ResourceSlot& slot = slots[i];

        VkDescriptorBufferInfo bufferInfo = {};
        bufferInfo.range = VK_WHOLE_SIZE;

        if (slot)
        {
            SLANG_RHI_ASSERT(slot.type == BindingType::Buffer);
            BufferImpl* buffer = checked_cast<BufferImpl*>(slot.resource.get());
            BufferRange bufferRange = buffer->resolveBufferRange(slot.bufferRange);
            bufferInfo.buffer = buffer->m_buffer.m_buffer;
            bufferInfo.offset = bufferRange.offset;
            bufferInfo.range = bufferRange.size;
        }

        VkWriteDescriptorSet write = {};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.descriptorCount = 1;
        write.descriptorType = descriptorType;
        write.dstArrayElement = uint32_t(i);
        write.dstBinding = offset.binding;
        write.dstSet = descriptorSet;
        write.pBufferInfo = &bufferInfo;

        writeDescriptor(context, write);
    }
}

void ShaderObjectImpl::writeTexelBufferDescriptor(
    BindingContext& context,
    BindingOffset const& offset,
    VkDescriptorType descriptorType,
    span<const ResourceSlot> slots
)
{
    VkDescriptorSet descriptorSet = context.bindable->descriptorSets[offset.bindingSet];

    Index count = slots.size();
    for (Index i = 0; i < count; ++i)
    {
        const ResourceSlot& slot = slots[i];

        VkBufferView bufferView = VK_NULL_HANDLE;

        if (slot)
        {
            SLANG_RHI_ASSERT(slot.type == BindingType::Buffer);
            BufferImpl* buffer = checked_cast<BufferImpl*>(slot.resource.get());
            bufferView = buffer->getView(slot.format, slot.bufferRange);
        }

        VkWriteDescriptorSet write = {};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.descriptorType = descriptorType;
        write.dstArrayElement = uint32_t(i);
        write.dstBinding = offset.binding;
        write.dstSet = descriptorSet;
        write.descriptorCount = 1;
        write.pTexelBufferView = &bufferView;
        writeDescriptor(context, write);
    }
}

void ShaderObjectImpl::writeTextureSamplerDescriptor(
    BindingContext& context,
    BindingOffset const& offset,
    VkDescriptorType descriptorType,
    span<const CombinedTextureSamplerSlot> slots
)
{
    VkDescriptorSet descriptorSet = context.bindable->descriptorSets[offset.bindingSet];

    Index count = slots.size();
    for (Index i = 0; i < count; ++i)
    {
        const CombinedTextureSamplerSlot& slot = slots[i];
        VkDescriptorImageInfo imageInfo = {};
        if (slot)
        {
            imageInfo.imageView = slot.textureView->getView().imageView;
            imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageInfo.sampler = slot.sampler->m_sampler;
        }

        VkWriteDescriptorSet write = {};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.descriptorCount = 1;
        write.descriptorType = descriptorType;
        write.dstArrayElement = uint32_t(i);
        write.dstBinding = offset.binding;
        write.dstSet = descriptorSet;
        write.pImageInfo = &imageInfo;

        writeDescriptor(context, write);
    }
}

void ShaderObjectImpl::writeAccelerationStructureDescriptor(
    BindingContext& context,
    BindingOffset const& offset,
    VkDescriptorType descriptorType,
    span<const ResourceSlot> slots
)
{
    VkDescriptorSet descriptorSet = context.bindable->descriptorSets[offset.bindingSet];

    Index count = slots.size();
    for (Index i = 0; i < count; ++i)
    {
        const ResourceSlot& slot = slots[i];

        VkWriteDescriptorSetAccelerationStructureKHR writeAS = {};
        writeAS.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
        writeAS.accelerationStructureCount = 1;
        VkAccelerationStructureKHR nullHandle = VK_NULL_HANDLE;

        if (slot)
        {
            SLANG_RHI_ASSERT(slot.type == BindingType::AccelerationStructure);
            AccelerationStructureImpl* accelerationStructure =
                checked_cast<AccelerationStructureImpl*>(slot.resource.get());
            writeAS.pAccelerationStructures = &accelerationStructure->m_vkHandle;
        }
        else
        {
            writeAS.pAccelerationStructures = &nullHandle;
        }

        VkWriteDescriptorSet write = {};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.descriptorCount = 1;
        write.descriptorType = descriptorType;
        write.dstArrayElement = uint32_t(i);
        write.dstBinding = offset.binding;
        write.dstSet = descriptorSet;
        write.pNext = &writeAS;
        writeDescriptor(context, write);
    }
}

void ShaderObjectImpl::writeTextureDescriptor(
    BindingContext& context,
    BindingOffset const& offset,
    VkDescriptorType descriptorType,
    span<const ResourceSlot> slots
)
{
    VkDescriptorSet descriptorSet = context.bindable->descriptorSets[offset.bindingSet];

    Index count = slots.size();
    for (Index i = 0; i < count; ++i)
    {
        const ResourceSlot& slot = slots[i];

        VkDescriptorImageInfo imageInfo = {};
        if (slot)
        {
            SLANG_RHI_ASSERT(slot.type == BindingType::TextureView);
            TextureViewImpl* textureView = checked_cast<TextureViewImpl*>(slot.resource.get());
            imageInfo.imageView = textureView->getView().imageView;
            imageInfo.imageLayout = descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
                                        ? VK_IMAGE_LAYOUT_GENERAL
                                        : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
        imageInfo.sampler = 0;

        VkWriteDescriptorSet write = {};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.descriptorCount = 1;
        write.descriptorType = descriptorType;
        write.dstArrayElement = uint32_t(i);
        write.dstBinding = offset.binding;
        write.dstSet = descriptorSet;
        write.pImageInfo = &imageInfo;

        writeDescriptor(context, write);
    }
}

void ShaderObjectImpl::writeSamplerDescriptor(
    BindingContext& context,
    BindingOffset const& offset,
    VkDescriptorType descriptorType,
    span<const RefPtr<SamplerImpl>> samplers
)
{
    VkDescriptorSet descriptorSet = context.bindable->descriptorSets[offset.bindingSet];

    Index count = samplers.size();
    for (Index i = 0; i < count; ++i)
    {
        auto sampler = samplers[i];
        VkDescriptorImageInfo imageInfo = {};
        imageInfo.imageView = 0;
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        if (sampler)
        {
            imageInfo.sampler = sampler->m_sampler;
        }
        else
        {
            imageInfo.sampler = context.device->m_defaultSampler;
        }

        VkWriteDescriptorSet write = {};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.descriptorCount = 1;
        write.descriptorType = descriptorType;
        write.dstArrayElement = uint32_t(i);
        write.dstBinding = offset.binding;
        write.dstSet = descriptorSet;
        write.pImageInfo = &imageInfo;

        writeDescriptor(context, write);
    }
}

Result ShaderObjectImpl::_ensureOrdinaryDataBufferCreatedIfNeeded(
    BindingContext& context,
    ShaderObjectLayoutImpl* specializedLayout
) const
{
    // TODO
    if (specializedLayout->getTotalOrdinaryDataSize() == 0)
    {
        return SLANG_OK;
    }

#if 0
    m_isConstantBufferDirty = false;
    m_constantBufferTransientHeap = context.transientHeap;
    m_constantBufferTransientHeapVersion = context.transientHeap->getVersion();

    m_constantBufferSize = specializedLayout->getTotalOrdinaryDataSize();
    if (m_constantBufferSize == 0)
    {
        return SLANG_OK;
    }

    // Once we have computed how large the buffer should be, we can allocate
    // it from the transient resource heap.
    //
    SLANG_RETURN_ON_FAIL(
        context.transientHeap->allocateConstantBuffer(m_constantBufferSize, m_constantBuffer, m_constantBufferOffset)
    );

    // Once the buffer is allocated, we can use `_writeOrdinaryData` to fill it in.
    //
    // Note that `_writeOrdinaryData` is potentially recursive in the case
    // where this object contains interface/existential-type fields, so we
    // don't need or want to inline it into this call site.
    //
    SLANG_RETURN_ON_FAIL(_writeOrdinaryData(
        context,
        checked_cast<BufferImpl*>(m_constantBuffer),
        m_constantBufferOffset,
        m_constantBufferSize,
        specializedLayout
    ));
#endif
    return SLANG_OK;
}

Result ShaderObjectImpl::bindAsValue(
    BindingContext& context,
    BindingOffset const& offset,
    ShaderObjectLayoutImpl* specializedLayout
) const
{
    // We start by iterating over the "simple" (non-sub-object) binding
    // ranges and writing them to the descriptor sets that are being
    // passed down.
    //
    for (auto bindingRangeInfo : specializedLayout->getBindingRanges())
    {
        BindingOffset rangeOffset = offset;

        auto baseIndex = bindingRangeInfo.baseIndex;
        auto count = (uint32_t)bindingRangeInfo.count;
        switch (bindingRangeInfo.bindingType)
        {
        case slang::BindingType::ConstantBuffer:
        case slang::BindingType::ParameterBlock:
        case slang::BindingType::ExistentialValue:
            break;

        case slang::BindingType::Texture:
            rangeOffset.bindingSet += bindingRangeInfo.setOffset;
            rangeOffset.binding += bindingRangeInfo.bindingOffset;
            writeTextureDescriptor(
                context,
                rangeOffset,
                VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                span(m_resources.data() + baseIndex, count)
            );
            break;
        case slang::BindingType::MutableTexture:
            rangeOffset.bindingSet += bindingRangeInfo.setOffset;
            rangeOffset.binding += bindingRangeInfo.bindingOffset;
            writeTextureDescriptor(
                context,
                rangeOffset,
                VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                span(m_resources.data() + baseIndex, count)
            );
            break;
        case slang::BindingType::CombinedTextureSampler:
            rangeOffset.bindingSet += bindingRangeInfo.setOffset;
            rangeOffset.binding += bindingRangeInfo.bindingOffset;
            writeTextureSamplerDescriptor(
                context,
                rangeOffset,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                span(m_combinedTextureSamplers.data() + baseIndex, count)
            );
            break;

        case slang::BindingType::Sampler:
            rangeOffset.bindingSet += bindingRangeInfo.setOffset;
            rangeOffset.binding += bindingRangeInfo.bindingOffset;
            writeSamplerDescriptor(
                context,
                rangeOffset,
                VK_DESCRIPTOR_TYPE_SAMPLER,
                span(m_samplers.data() + baseIndex, count)
            );
            break;

        case slang::BindingType::RawBuffer:
        case slang::BindingType::MutableRawBuffer:
            rangeOffset.bindingSet += bindingRangeInfo.setOffset;
            rangeOffset.binding += bindingRangeInfo.bindingOffset;
            writePlainBufferDescriptor(
                context,
                rangeOffset,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                span(m_resources.data() + baseIndex, count)
            );
            break;

        case slang::BindingType::TypedBuffer:
            rangeOffset.bindingSet += bindingRangeInfo.setOffset;
            rangeOffset.binding += bindingRangeInfo.bindingOffset;
            writeTexelBufferDescriptor(
                context,
                rangeOffset,
                VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,
                span(m_resources.data() + baseIndex, count)
            );
            break;
        case slang::BindingType::MutableTypedBuffer:
            rangeOffset.bindingSet += bindingRangeInfo.setOffset;
            rangeOffset.binding += bindingRangeInfo.bindingOffset;
            writeTexelBufferDescriptor(
                context,
                rangeOffset,
                VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,
                span(m_resources.data() + baseIndex, count)
            );
            break;
        case slang::BindingType::RayTracingAccelerationStructure:
            rangeOffset.bindingSet += bindingRangeInfo.setOffset;
            rangeOffset.binding += bindingRangeInfo.bindingOffset;
            writeAccelerationStructureDescriptor(
                context,
                rangeOffset,
                VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
                span(m_resources.data() + baseIndex, count)
            );
            break;
        case slang::BindingType::VaryingInput:
        case slang::BindingType::VaryingOutput:
            break;

        default:
            SLANG_RHI_ASSERT_FAILURE("Unsupported binding type");
            return SLANG_FAIL;
            break;
        }
    }

    // Once we've handled the simple binding ranges, we move on to the
    // sub-object ranges, which are generally more involved.
    //
    for (auto const& subObjectRange : specializedLayout->getSubObjectRanges())
    {
        auto const& bindingRangeInfo = specializedLayout->getBindingRange(subObjectRange.bindingRangeIndex);
        auto count = bindingRangeInfo.count;
        auto subObjectIndex = bindingRangeInfo.subObjectIndex;

        auto subObjectLayout = subObjectRange.layout;

        // The starting offset to use for the sub-object
        // has already been computed and stored as part
        // of the layout, so we can get to the starting
        // offset for the range easily.
        //
        BindingOffset rangeOffset = offset;
        rangeOffset += subObjectRange.offset;

        BindingOffset rangeStride = subObjectRange.stride;

        switch (bindingRangeInfo.bindingType)
        {
        case slang::BindingType::ConstantBuffer:
        {
            BindingOffset objOffset = rangeOffset;
            for (Index i = 0; i < count; ++i)
            {
                // Binding a constant buffer sub-object is simple enough:
                // we just call `bindAsConstantBuffer` on it to bind
                // the ordinary data buffer (if needed) and any other
                // bindings it recursively contains.
                //
                ShaderObjectImpl* subObject = m_objects[subObjectIndex + i];
                subObject->bindAsConstantBuffer(context, objOffset, subObjectLayout);

                // When dealing with arrays of sub-objects, we need to make
                // sure to increment the offset for each subsequent object
                // by the appropriate stride.
                //
                objOffset += rangeStride;
            }
        }
        break;
        case slang::BindingType::ParameterBlock:
        {
            BindingOffset objOffset = rangeOffset;
            for (Index i = 0; i < count; ++i)
            {
                // The case for `ParameterBlock<X>` is not that different
                // from `ConstantBuffer<X>`, except that we call `bindAsParameterBlock`
                // instead (understandably).
                //
                ShaderObjectImpl* subObject = m_objects[subObjectIndex + i];
                subObject->bindAsParameterBlock(context, objOffset, subObjectLayout);
            }
        }
        break;

        case slang::BindingType::ExistentialValue:
            // Interface/existential-type sub-object ranges are the most complicated case.
            //
            // First, we can only bind things if we have static specialization information
            // to work with, which is exactly the case where `subObjectLayout` will be
            // non-null.
            //
            if (subObjectLayout)
            {
                // Second, the offset where we want to start binding for existential-type
                // ranges is a bit different, because we don't wnat to bind at the "primary"
                // offset that got passed down, but instead at the "pending" offset.
                //
                // For the purposes of nested binding, what used to be the pending offset
                // will now be used as the primary offset.
                //
                SimpleBindingOffset objOffset = rangeOffset.pending;
                SimpleBindingOffset objStride = rangeStride.pending;
                for (Index i = 0; i < count; ++i)
                {
                    // An existential-type sub-object is always bound just as a value,
                    // which handles its nested bindings and descriptor sets, but
                    // does not deal with ordianry data. The ordinary data should
                    // have been handled as part of the buffer for a parent object
                    // already.
                    //
                    ShaderObjectImpl* subObject = m_objects[subObjectIndex + i];
                    subObject->bindAsValue(context, BindingOffset(objOffset), subObjectLayout);
                    objOffset += objStride;
                }
            }
            break;
        case slang::BindingType::RawBuffer:
        case slang::BindingType::MutableRawBuffer:
            // No action needed for sub-objects bound though a `StructuredBuffer`.
            break;
        default:
            SLANG_RHI_ASSERT_FAILURE("Unsupported sub-object type");
            return SLANG_FAIL;
            break;
        }
    }

    return SLANG_OK;
}

Result ShaderObjectImpl::allocateDescriptorSets(
    BindingContext& context,
    BindingOffset const& offset,
    ShaderObjectLayoutImpl* specializedLayout
) const
{
    SLANG_RHI_ASSERT(specializedLayout->getOwnDescriptorSets().size() <= 1);
    // The number of sets to allocate and their layouts was already pre-computed
    // as part of the shader object layout, so we use that information here.
    //
    for (auto descriptorSetInfo : specializedLayout->getOwnDescriptorSets())
    {
        auto descriptorSetHandle =
            context.descriptorSetAllocator->allocate(descriptorSetInfo.descriptorSetLayout).handle;

        // For each set, we need to write it into the set of descriptor sets
        // being used for binding. This is done both so that other steps
        // in binding can find the set to fill it in, but also so that
        // we can bind all the descriptor sets to the pipeline when the
        // time comes.
        //
        context.bindable->descriptorSets.push_back(descriptorSetHandle);
    }

    return SLANG_OK;
}

Result ShaderObjectImpl::bindAsParameterBlock(
    BindingContext& context,
    BindingOffset const& inOffset,
    ShaderObjectLayoutImpl* specializedLayout
) const
{
    // Because we are binding into a nested parameter block,
    // any texture/buffer/sampler bindings will now want to
    // write into the sets we allocate for this object and
    // not the sets for any parent object(s).
    //
    BindingOffset offset = inOffset;
    offset.bindingSet = (uint32_t)context.bindable->descriptorSets.size();
    offset.binding = 0;

    // TODO: We should also be writing to `offset.pending` here,
    // because any resource/sampler bindings related to "pending"
    // data should *also* be writing into the chosen set.
    //
    // The challenge here is that we need to compute the right
    // value for `offset.pending.binding`, so that it writes after
    // all the other bindings.

    // Writing the bindings for a parameter block is relatively easy:
    // we just need to allocate the descriptor set(s) needed for this
    // object and then fill it in like a `ConstantBuffer<X>`.
    //
    SLANG_RETURN_ON_FAIL(allocateDescriptorSets(context, offset, specializedLayout));

    SLANG_RHI_ASSERT(offset.bindingSet < (uint32_t)context.bindable->descriptorSets.size());
    SLANG_RETURN_ON_FAIL(bindAsConstantBuffer(context, offset, specializedLayout));

    return SLANG_OK;
}

Result ShaderObjectImpl::bindOrdinaryDataBufferIfNeeded(
    BindingContext& context,
    BindingOffset& ioOffset,
    ShaderObjectLayoutImpl* specializedLayout
) const
{
    uint32_t constantBufferSize = specializedLayout->getTotalOrdinaryDataSize();
    if (constantBufferSize == 0)
    {
        return SLANG_OK;
    }

    // Once we have computed how large the buffer should be, allocate it.
    //
    BufferImpl* constantBuffer = nullptr;
    size_t constantBufferOffset = 0;
    SLANG_RETURN_ON_FAIL(context.allocateConstantBuffer(constantBufferSize, constantBuffer, constantBufferOffset));

    // Once the buffer is allocated, we can use `_writeOrdinaryData` to fill it in.
    //
    // Note that `_writeOrdinaryData` is potentially recursive in the case
    // where this object contains interface/existential-type fields, so we
    // don't need or want to inline it into this call site.
    //
    SLANG_RETURN_ON_FAIL(
        _writeOrdinaryData(context, constantBuffer, constantBufferOffset, constantBufferSize, specializedLayout)
    );

    // If we did indeed need/create a buffer, then we must bind it into
    // the given `descriptorSet` and update the base range index for
    // subsequent binding operations to account for it.
    //
    writeBufferDescriptor(
        context,
        ioOffset,
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        constantBuffer,
        constantBufferOffset,
        constantBufferSize
    );
    ioOffset.binding++;

    return SLANG_OK;
}

Result ShaderObjectImpl::bindAsConstantBuffer(
    BindingContext& context,
    BindingOffset const& inOffset,
    ShaderObjectLayoutImpl* specializedLayout
) const
{
    // To bind an object as a constant buffer, we first
    // need to bind its ordinary data (if any) into an
    // ordinary data buffer, and then bind it as a "value"
    // which handles any of its recursively-contained bindings.
    //
    // The one detail is taht when binding the ordinary data
    // buffer we need to adjust the `binding` index used for
    // subsequent operations based on whether or not an ordinary
    // data buffer was used (and thus consumed a `binding`).
    //
    BindingOffset offset = inOffset;
    SLANG_RETURN_ON_FAIL(bindOrdinaryDataBufferIfNeeded(context, /*inout*/ offset, specializedLayout));
    SLANG_RETURN_ON_FAIL(bindAsValue(context, offset, specializedLayout));
    return SLANG_OK;
}

void ShaderObjectImpl::setResourceStates(StateTracking& stateTracking) const
{
    for (const ResourceSlot& slot : m_resources)
    {
        switch (slot.type)
        {
        case BindingType::Buffer:
            stateTracking.setBufferState(checked_cast<BufferImpl*>(slot.resource.get()), slot.requiredState);
            break;
        case BindingType::TextureView:
        {
            TextureViewImpl* textureView = checked_cast<TextureViewImpl*>(slot.resource.get());
            stateTracking
                .setTextureState(textureView->m_texture, textureView->m_desc.subresourceRange, slot.requiredState);
            break;
        }
        case BindingType::AccelerationStructure:
            // TODO STATE_TRACKING need state transition?
            break;
        }
    }

    for (const CombinedTextureSamplerSlot& slot : m_combinedTextureSamplers)
    {
        if (slot.textureView)
        {
            stateTracking.setTextureState(
                slot.textureView->m_texture,
                slot.textureView->m_desc.subresourceRange,
                ResourceState::ShaderResource
            );
        }
    }

    for (auto& subObject : m_objects)
    {
        if (subObject)
        {
            subObject->setResourceStates(stateTracking);
        }
    }
}

Result ShaderObjectImpl::_getSpecializedLayout(ShaderObjectLayoutImpl** outLayout)
{
    if (!m_specializedLayout)
    {
        SLANG_RETURN_ON_FAIL(_createSpecializedLayout(m_specializedLayout.writeRef()));
    }
    returnRefPtr(outLayout, m_specializedLayout);
    return SLANG_OK;
}

Result ShaderObjectImpl::_createSpecializedLayout(ShaderObjectLayoutImpl** outLayout)
{
    ExtendedShaderObjectType extendedType;
    SLANG_RETURN_ON_FAIL(getSpecializedShaderObjectType(&extendedType));

    auto device = getDevice();
    RefPtr<ShaderObjectLayoutImpl> layout;
    SLANG_RETURN_ON_FAIL(device->getShaderObjectLayout(
        m_layout->m_slangSession,
        extendedType.slangType,
        m_layout->getContainerType(),
        (ShaderObjectLayout**)layout.writeRef()
    ));

    returnRefPtrMove(outLayout, layout);
    return SLANG_OK;
}

Result EntryPointShaderObject::create(
    DeviceImpl* device,
    EntryPointLayout* layout,
    EntryPointShaderObject** outShaderObject
)
{
    RefPtr<EntryPointShaderObject> object = new EntryPointShaderObject();
    SLANG_RETURN_ON_FAIL(object->init(device, layout));

    returnRefPtrMove(outShaderObject, object);
    return SLANG_OK;
}

EntryPointLayout* EntryPointShaderObject::getLayout()
{
    return checked_cast<EntryPointLayout*>(m_layout.Ptr());
}

Result EntryPointShaderObject::bindAsEntryPoint(
    BindingContext& context,
    BindingOffset const& inOffset,
    EntryPointLayout* layout
)
{
    BindingOffset offset = inOffset;

    // Any ordinary data in an entry point is assumed to be allocated
    // as a push-constant range.
    //
    // TODO: Can we make this operation not bake in that assumption?
    //
    // TODO: Can/should this function be renamed as just `bindAsPushConstantBuffer`?
    //
    if (m_data.getCount())
    {
        // The index of the push constant range to bind should be
        // passed down as part of the `offset`, and we will increment
        // it here so that any further recursively-contained push-constant
        // ranges use the next index.
        //
        auto pushConstantRangeIndex = offset.pushConstantRange++;

        // Information about the push constant ranges (including offsets
        // and stage flags) was pre-computed for the entire program and
        // stored on the binding context.
        //
        auto const& pushConstantRange = context.pushConstantRanges[pushConstantRangeIndex];

        // We expect that the size of the range as reflected matches the
        // amount of ordinary data stored on this object.
        //
        // TODO: This would not be the case if specialization for interface-type
        // parameters led to the entry point having "pending" ordinary data.
        //
        SLANG_RHI_ASSERT(pushConstantRange.size == (uint32_t)m_data.getCount());

        auto pushConstantData = m_data.getBuffer();

        context.bindable->pushConstants.push_back({pushConstantRange, pushConstantData});
    }

    // Any remaining bindings in the object can be handled through the
    // "value" case.
    //
    SLANG_RETURN_ON_FAIL(bindAsValue(context, offset, layout));
    return SLANG_OK;
}

Result EntryPointShaderObject::init(DeviceImpl* device, EntryPointLayout* layout)
{
    SLANG_RETURN_ON_FAIL(Super::init(device, layout));
    return SLANG_OK;
}

RootShaderObjectLayout* RootShaderObjectImpl::getLayout()
{
    return checked_cast<RootShaderObjectLayout*>(m_layout.Ptr());
}

RootShaderObjectLayout* RootShaderObjectImpl::getSpecializedLayout()
{
    RefPtr<ShaderObjectLayoutImpl> specializedLayout;
    _getSpecializedLayout(specializedLayout.writeRef());
    return checked_cast<RootShaderObjectLayout*>(m_specializedLayout.Ptr());
}

std::vector<RefPtr<EntryPointShaderObject>> const& RootShaderObjectImpl::getEntryPoints() const
{
    return m_entryPoints;
}

GfxCount RootShaderObjectImpl::getEntryPointCount()
{
    return (GfxCount)m_entryPoints.size();
}

Result RootShaderObjectImpl::getEntryPoint(Index index, IShaderObject** outEntryPoint)
{
    returnComPtr(outEntryPoint, m_entryPoints[index]);
    return SLANG_OK;
}

void RootShaderObjectImpl::setResourceStates(StateTracking& stateTracking)
{
    ShaderObjectImpl::setResourceStates(stateTracking);
    for (auto& entryPoint : m_entryPoints)
    {
        entryPoint->setResourceStates(stateTracking);
    }
}

Result RootShaderObjectImpl::bindAsRoot(BindingContext& context, RootShaderObjectLayout* layout)
{
    BindingOffset offset = {};
    offset.pending = layout->getPendingDataOffset();

    // Note: the operations here are quite similar to what `bindAsParameterBlock` does.
    // The key difference in practice is that we do *not* make use of the adjustment
    // that `bindOrdinaryDataBufferIfNeeded` applied to the offset passed into it.
    //
    // The reason for this difference in behavior is that the layout information
    // for root shader parameters is in practice *already* offset appropriately
    // (so that it ends up using absolute offsets).
    //
    // TODO: One more wrinkle here is that the `ordinaryDataBufferOffset` below
    // might not be correct if `binding=0,set=0` was already claimed via explicit
    // binding information. We should really be getting the offset information for
    // the ordinary data buffer directly from the reflection information for
    // the global scope.

    SLANG_RETURN_ON_FAIL(allocateDescriptorSets(context, offset, layout));

    BindingOffset ordinaryDataBufferOffset = offset;
    SLANG_RETURN_ON_FAIL(bindOrdinaryDataBufferIfNeeded(context, ordinaryDataBufferOffset, layout));

    SLANG_RETURN_ON_FAIL(bindAsValue(context, offset, layout));

    auto entryPointCount = layout->getEntryPoints().size();
    for (Index i = 0; i < entryPointCount; ++i)
    {
        auto entryPoint = m_entryPoints[i];
        auto const& entryPointInfo = layout->getEntryPoint(i);

        // Note: we do *not* need to add the entry point offset
        // information to the global `offset` because the
        // `RootShaderObjectLayout` has already baked any offsets
        // from the global layout into the `entryPointInfo`.

        entryPoint->bindAsEntryPoint(context, entryPointInfo.offset, entryPointInfo.layout);
    }

    return SLANG_OK;
}

Result RootShaderObjectImpl::collectSpecializationArgs(ExtendedShaderObjectTypeList& args)
{
    SLANG_RETURN_ON_FAIL(ShaderObjectImpl::collectSpecializationArgs(args));
    for (auto& entryPoint : m_entryPoints)
    {
        SLANG_RETURN_ON_FAIL(entryPoint->collectSpecializationArgs(args));
    }
    return SLANG_OK;
}

Result RootShaderObjectImpl::init(DeviceImpl* device, RootShaderObjectLayout* layout)
{
    SLANG_RETURN_ON_FAIL(Super::init(device, layout));
    m_specializedLayout = nullptr;
    m_entryPoints.clear();
    for (auto entryPointInfo : layout->getEntryPoints())
    {
        RefPtr<EntryPointShaderObject> entryPoint;
        SLANG_RETURN_ON_FAIL(EntryPointShaderObject::create(device, entryPointInfo.layout, entryPoint.writeRef()));
        m_entryPoints.push_back(entryPoint);
    }

    return SLANG_OK;
}

Result RootShaderObjectImpl::_createSpecializedLayout(ShaderObjectLayoutImpl** outLayout)
{
    ExtendedShaderObjectTypeList specializationArgs;
    SLANG_RETURN_ON_FAIL(collectSpecializationArgs(specializationArgs));

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
    // form that of `compose(specialize(A,X), speciealize(B,Y))`, even when both are
    // semantically equivalent programs.
    //
    // Right now we are using the first option: we are first generating a full composition
    // of all the code we plan to use (global scope plus all entry points), and then
    // specializing it to the concatenated specialization argumenst for all of that.
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
    // parmaeters, but their layouts are also independent of one another.
    //
    // Furthermore, in this example, loading another entry point into the system would not
    // rquire re-computing the layouts (or generated kernel code) for any of the entry
    // points that had already been loaded (in contrast to a compose-then-specialize
    // approach).
    //
    ComPtr<slang::IComponentType> specializedComponentType;
    ComPtr<slang::IBlob> diagnosticBlob;
    auto result = getLayout()->getSlangProgram()->specialize(
        specializationArgs.components.data(),
        specializationArgs.getCount(),
        specializedComponentType.writeRef(),
        diagnosticBlob.writeRef()
    );

    // TODO: print diagnostic message via debug output interface.

    if (result != SLANG_OK)
        return result;

    auto slangSpecializedLayout = specializedComponentType->getLayout();
    RefPtr<RootShaderObjectLayout> specializedLayout;
    RootShaderObjectLayout::create(
        checked_cast<DeviceImpl*>(getDevice()),
        specializedComponentType,
        slangSpecializedLayout,
        specializedLayout.writeRef()
    );

    // Note: Computing the layout for the specialized program will have also computed
    // the layouts for the entry points, and we really need to attach that information
    // to them so that they don't go and try to compute their own specializations.
    //
    // TODO: Well, if we move to the specialization model described above then maybe
    // we *will* want entry points to do their own specialization work...
    //
    auto entryPointCount = m_entryPoints.size();
    for (Index i = 0; i < entryPointCount; ++i)
    {
        auto entryPointInfo = specializedLayout->getEntryPoint(i);
        auto entryPointVars = m_entryPoints[i];

        entryPointVars->m_specializedLayout = entryPointInfo.layout;
    }

    returnRefPtrMove(outLayout, specializedLayout);
    return SLANG_OK;
}

} // namespace rhi::vk
