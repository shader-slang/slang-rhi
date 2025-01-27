#include "wgpu-shader-object.h"
#include "wgpu-device.h"
#include "wgpu-buffer.h"
#include "wgpu-texture.h"
#include "wgpu-sampler.h"
#include "wgpu-command.h"

namespace rhi::wgpu {

inline void writeDescriptor(BindingDataBuilder& builder, uint32_t bindingSet, const WGPUBindGroupEntry& write)
{
    SLANG_RHI_ASSERT(bindingSet < builder.m_entries.size());
    builder.m_entries[bindingSet].push_back(write);
}

inline void writeBufferDescriptor(
    BindingDataBuilder& builder,
    const BindingOffset& offset,
    BufferImpl* buffer,
    uint64_t bufferOffset,
    uint64_t bufferSize
)
{
    WGPUBindGroupEntry entry = {};
    entry.binding = offset.binding;
    entry.buffer = buffer->m_buffer;
    entry.offset = bufferOffset;
    entry.size = bufferSize;
    writeDescriptor(builder, offset.bindingSet, entry);
}

inline void writeBufferDescriptor(BindingDataBuilder& builder, const BindingOffset& offset, BufferImpl* buffer)
{
    writeBufferDescriptor(builder, offset, buffer, 0, buffer->m_desc.size);
}

inline void writeBufferDescriptor(
    BindingDataBuilder& builder,
    const BindingOffset& offset,
    span<const ResourceSlot> slots
)
{
    for (size_t i = 0; i < slots.size(); ++i)
    {
        const ResourceSlot& slot = slots[i];

        WGPUBindGroupEntry entry = {};
        entry.binding = offset.binding + i;
        entry.buffer = checked_cast<BufferImpl*>(slot.resource.get())->m_buffer;
        entry.offset = slot.bufferRange.offset;
        entry.size = slot.bufferRange.size;
        writeDescriptor(builder, offset.bindingSet, entry);
    }
}

inline void writeTextureDescriptor(
    BindingDataBuilder& builder,
    const BindingOffset& offset,
    span<const ResourceSlot> slots
)
{
    for (size_t i = 0; i < slots.size(); ++i)
    {
        const ResourceSlot& slot = slots[i];

        WGPUBindGroupEntry entry = {};
        entry.binding = offset.binding + i;
        entry.textureView = checked_cast<TextureViewImpl*>(slot.resource.get())->m_textureView;
        writeDescriptor(builder, offset.bindingSet, entry);
    }
}

inline void writeSamplerDescriptor(
    BindingDataBuilder& builder,
    const BindingOffset& offset,
    span<const ResourceSlot> slots
)
{
    for (size_t i = 0; i < slots.size(); ++i)
    {
        const ResourceSlot& slot = slots[i];

        WGPUBindGroupEntry entry = {};
        entry.binding = offset.binding + i;
        entry.sampler = checked_cast<SamplerImpl*>(slot.resource.get())->m_sampler;
        writeDescriptor(builder, offset.bindingSet, entry);
    }
}

Result BindingDataBuilder::bindAsRoot(
    RootShaderObject* shaderObject,
    RootShaderObjectLayoutImpl* specializedLayout,
    BindingDataImpl*& outBindingData
)
{
    // Create a new set of binding data to populate.
    // TODO: In the future we should lookup the cache for existing
    // binding data and reuse that if possible.
    BindingDataImpl* bindingData = m_allocator->allocate<BindingDataImpl>();
    m_bindingData = bindingData;
    m_bindingCache->bindingData.push_back(bindingData);

    m_bindGroupLayouts = specializedLayout->m_bindGroupLayouts;

    BindingOffset offset = {};
    offset.pending = specializedLayout->m_pendingDataOffset;

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

    SLANG_RETURN_ON_FAIL(allocateDescriptorSets(shaderObject, offset, specializedLayout));

    BindingOffset ordinaryDataBufferOffset = offset;
    SLANG_RETURN_ON_FAIL(bindOrdinaryDataBufferIfNeeded(shaderObject, ordinaryDataBufferOffset, specializedLayout));

    SLANG_RETURN_ON_FAIL(bindAsValue(shaderObject, offset, specializedLayout));

    size_t entryPointCount = specializedLayout->m_entryPoints.size();
    for (size_t i = 0; i < entryPointCount; ++i)
    {
        auto entryPoint = shaderObject->m_entryPoints[i];
        const auto& entryPointInfo = specializedLayout->m_entryPoints[i];
        EntryPointLayout* entryPointLayout = entryPointInfo.layout;

        // Note: we do *not* need to add the entry point offset
        // information to the global `offset` because the
        // `RootShaderObjectLayout` has already baked any offsets
        // from the global layout into the `entryPointInfo`.

        SLANG_RETURN_ON_FAIL(bindAsEntryPoint(entryPoint, entryPointInfo.offset, entryPointLayout));
    }

    SLANG_RETURN_ON_FAIL(createBindGroups());

    outBindingData = bindingData;

    return SLANG_OK;
}

Result BindingDataBuilder::allocateDescriptorSets(
    ShaderObject* shaderObject,
    const BindingOffset& offset,
    ShaderObjectLayoutImpl* specializedLayout
)
{
    SLANG_RHI_ASSERT(specializedLayout->getOwnDescriptorSets().size() <= 1);

    const auto& descriptorSets = specializedLayout->getOwnDescriptorSets();
    for (size_t i = 0; i < descriptorSets.size(); ++i)
    {
        m_entries.push_back(std::vector<WGPUBindGroupEntry>());
        auto& newEntries = m_entries.back();
        newEntries.reserve(descriptorSets[i].entries.size());
    }
    return SLANG_OK;
}

Result BindingDataBuilder::createBindGroups()
{
    m_bindingData->bindGroupCount = m_entries.size();
    m_bindingData->bindGroups = m_allocator->allocate<WGPUBindGroup>(m_bindingData->bindGroupCount);

    for (size_t i = 0; i < m_entries.size(); ++i)
    {
        WGPUBindGroupDescriptor desc = {};
        desc.layout = m_bindGroupLayouts[i];
        desc.entries = m_entries[i].data();
        desc.entryCount = (uint32_t)m_entries[i].size();
        WGPUBindGroup bindGroup = m_device->m_ctx.api.wgpuDeviceCreateBindGroup(m_device->m_ctx.device, &desc);
        if (!bindGroup)
        {
            return SLANG_FAIL;
        }
        m_bindingData->bindGroups[i] = bindGroup;
    }
    return SLANG_OK;
}

Result BindingDataBuilder::bindAsValue(
    ShaderObject* shaderObject,
    const BindingOffset& offset,
    ShaderObjectLayoutImpl* specializedLayout
)
{
    // We start by iterating over the "simple" (non-sub-object) binding
    // ranges and writing them to the descriptor sets that are being
    // passed down.
    //
    for (const auto& bindingRangeInfo : specializedLayout->m_bindingRanges)
    {
        BindingOffset rangeOffset = offset;

        uint32_t slotIndex = bindingRangeInfo.slotIndex;
        uint32_t count = bindingRangeInfo.count;
        switch (bindingRangeInfo.bindingType)
        {
        case slang::BindingType::ConstantBuffer:
        case slang::BindingType::ParameterBlock:
        case slang::BindingType::ExistentialValue:
            break;

        case slang::BindingType::Texture:
        case slang::BindingType::MutableTexture:
            rangeOffset.bindingSet += bindingRangeInfo.setOffset;
            rangeOffset.binding += bindingRangeInfo.bindingOffset;
            writeTextureDescriptor(*this, rangeOffset, span(shaderObject->m_slots.data() + slotIndex, count));
            break;
        case slang::BindingType::Sampler:
            rangeOffset.bindingSet += bindingRangeInfo.setOffset;
            rangeOffset.binding += bindingRangeInfo.bindingOffset;
            writeSamplerDescriptor(*this, rangeOffset, span(shaderObject->m_slots.data() + slotIndex, count));
            break;

        case slang::BindingType::RawBuffer:
        case slang::BindingType::MutableRawBuffer:
        case slang::BindingType::TypedBuffer:
        case slang::BindingType::MutableTypedBuffer:
            rangeOffset.bindingSet += bindingRangeInfo.setOffset;
            rangeOffset.binding += bindingRangeInfo.bindingOffset;
            writeBufferDescriptor(*this, rangeOffset, span(shaderObject->m_slots.data() + slotIndex, count));
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
    for (const auto& subObjectRange : specializedLayout->m_subObjectRanges)
    {
        const auto& bindingRangeInfo = specializedLayout->m_bindingRanges[subObjectRange.bindingRangeIndex];
        uint32_t count = bindingRangeInfo.count;
        uint32_t subObjectIndex = bindingRangeInfo.subObjectIndex;

        ShaderObjectLayoutImpl* subObjectLayout = subObjectRange.layout;

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
            for (uint32_t i = 0; i < count; ++i)
            {
                // Binding a constant buffer sub-object is simple enough:
                // we just call `bindAsConstantBuffer` on it to bind
                // the ordinary data buffer (if needed) and any other
                // bindings it recursively contains.
                //
                ShaderObject* subObject = shaderObject->m_objects[subObjectIndex + i];
                SLANG_RETURN_ON_FAIL(bindAsConstantBuffer(subObject, objOffset, subObjectLayout));

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
            for (uint32_t i = 0; i < count; ++i)
            {
                // The case for `ParameterBlock<X>` is not that different
                // from `ConstantBuffer<X>`, except that we call `bindAsParameterBlock`
                // instead (understandably).
                //
                ShaderObject* subObject = shaderObject->m_objects[subObjectIndex + i];
                SLANG_RETURN_ON_FAIL(bindAsParameterBlock(subObject, objOffset, subObjectLayout));
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
                for (uint32_t i = 0; i < count; ++i)
                {
                    // An existential-type sub-object is always bound just as a value,
                    // which handles its nested bindings and descriptor sets, but
                    // does not deal with ordianry data. The ordinary data should
                    // have been handled as part of the buffer for a parent object
                    // already.
                    //
                    ShaderObject* subObject = shaderObject->m_objects[subObjectIndex + i];
                    SLANG_RETURN_ON_FAIL(bindAsValue(subObject, BindingOffset(objOffset), subObjectLayout));
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

Result BindingDataBuilder::bindAsParameterBlock(
    ShaderObject* shaderObject,
    const BindingOffset& inOffset,
    ShaderObjectLayoutImpl* specializedLayout
)
{
    // Because we are binding into a nested parameter block,
    // any texture/buffer/sampler bindings will now want to
    // write into the sets we allocate for this object and
    // not the sets for any parent object(s).
    //
    BindingOffset offset = inOffset;
    offset.bindingSet = (uint32_t)m_entries.size();
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
    SLANG_RETURN_ON_FAIL(allocateDescriptorSets(shaderObject, offset, specializedLayout));

    SLANG_RHI_ASSERT(offset.bindingSet < (uint32_t)m_entries.size());
    SLANG_RETURN_ON_FAIL(bindAsConstantBuffer(shaderObject, offset, specializedLayout));

    return SLANG_OK;
}

Result BindingDataBuilder::bindOrdinaryDataBufferIfNeeded(
    ShaderObject* shaderObject,
    BindingOffset& ioOffset,
    ShaderObjectLayoutImpl* specializedLayout
)
{
    auto bufferSize = specializedLayout->getTotalOrdinaryDataSize();
    if (bufferSize == 0)
        return SLANG_OK;

    ConstantBufferPool::Allocation allocation;
    SLANG_RETURN_ON_FAIL(m_constantBufferPool->allocate(bufferSize, allocation));
    SLANG_RETURN_ON_FAIL(shaderObject->writeOrdinaryData(allocation.mappedData, bufferSize, specializedLayout));

    writeBufferDescriptor(*this, ioOffset, allocation.buffer, allocation.offset, bufferSize);
    ioOffset.binding++;

    return SLANG_OK;
}

Result BindingDataBuilder::bindAsConstantBuffer(
    ShaderObject* shaderObject,
    const BindingOffset& inOffset,
    ShaderObjectLayoutImpl* specializedLayout
)
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
    SLANG_RETURN_ON_FAIL(bindOrdinaryDataBufferIfNeeded(shaderObject, /*inout*/ offset, specializedLayout));
    SLANG_RETURN_ON_FAIL(bindAsValue(shaderObject, offset, specializedLayout));
    return SLANG_OK;
}

Result BindingDataBuilder::bindAsEntryPoint(
    ShaderObject* shaderObject,
    const BindingOffset& inOffset,
    EntryPointLayout* specializedLayout
)
{
    // First bind the constant buffer for ordinary uniform parameters defined in the entry point.
    {
        BindingOffset offset = inOffset;
        SLANG_RETURN_ON_FAIL(bindOrdinaryDataBufferIfNeeded(shaderObject, /*inout*/ offset, specializedLayout));
    }

    // Bind the remaining resource parameters.
    {
        // The binding layout for a non-resource entrypoint parameter already has offset baked in for
        // the builtin constant buffer for the ordinary uniform parameters (if any), so we use the
        // initial offset as-is.
        BindingOffset offset1 = inOffset;
        SLANG_RETURN_ON_FAIL(bindAsValue(shaderObject, offset1, specializedLayout));
    }
    return SLANG_OK;
}

void BindingDataImpl::release(DeviceImpl* device)
{
    for (size_t i = 0; i < bindGroupCount; ++i)
    {
        device->m_ctx.api.wgpuBindGroupRelease(bindGroups[i]);
    }
}

void BindingCache::reset(DeviceImpl* device)
{
    for (auto data : bindingData)
    {
        data->release(device);
    }
}

} // namespace rhi::wgpu
