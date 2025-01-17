#include "d3d12-shader-object.h"
#include "d3d12-buffer.h"
#include "d3d12-texture.h"
#include "d3d12-device.h"
#include "d3d12-helper-functions.h"
#include "d3d12-sampler.h"
#include "d3d12-shader-object-layout.h"
#include "d3d12-acceleration-structure.h"

namespace rhi::d3d12 {

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

// TODO: Change Index to Offset/Size?
Result ShaderObjectImpl::setData(const ShaderOffset& inOffset, const void* data, size_t inSize)
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

    m_isConstantBufferDirty = true;

    return SLANG_OK;
}

Result ShaderObjectImpl::setObject(const ShaderOffset& offset, IShaderObject* object)
{
    SLANG_RETURN_ON_FAIL(requireNotFinalized());
    SLANG_RETURN_ON_FAIL(Super::setObject(offset, object));
    return SLANG_OK;
}

Result ShaderObjectImpl::init(
    DeviceImpl* device,
    ShaderObjectLayoutImpl* layout,
    DescriptorHeapReference viewHeap,
    DescriptorHeapReference samplerHeap
)
{
    m_device = device;

    m_layout = layout;

    m_isConstantBufferDirty = true;

    // If the layout tells us that there is any uniform data,
    // then we will allocate a CPU memory buffer to hold that data
    // while it is being set from the host.
    //
    // Once the user is done setting the parameters/fields of this
    // shader object, we will produce a GPU-memory version of the
    // uniform data (which includes values from this object and
    // any existential-type sub-objects).
    //
    size_t uniformSize = layout->getElementTypeLayout()->getSize();
    if (uniformSize)
    {
        m_data.setCount(uniformSize);
        memset(m_data.getBuffer(), 0, uniformSize);
    }
    m_rootArguments.resize(layout->getOwnUserRootParameterCount());
    memset(m_rootArguments.data(), 0, sizeof(D3D12_GPU_VIRTUAL_ADDRESS) * m_rootArguments.size());
    // Each shader object will own CPU descriptor heap memory
    // for any resource or sampler descriptors it might store
    // as part of its value.
    //
    // This allocate includes a reservation for any constant
    // buffer descriptor pertaining to the ordinary data,
    // but does *not* include any descriptors that are managed
    // as part of sub-objects.
    //
    if (auto resourceCount = layout->getResourceSlotCount())
    {
        m_descriptorSet.resourceTable.allocate(viewHeap, resourceCount);

        // We must also ensure that the memory for any resources
        // referenced by descriptors in this object does not get
        // freed while the object is still live.
        //
        // The doubling here is because any buffer resource could
        // have a counter buffer associated with it, which we
        // also need to ensure isn't destroyed prematurely.
        m_boundResources.resize(resourceCount);
    }
    if (auto samplerCount = layout->getSamplerSlotCount())
    {
        m_descriptorSet.samplerTable.allocate(samplerHeap, samplerCount);
    }

    // If the layout specifies that we have any sub-objects, then
    // we need to size the array to account for them.
    //
    Index subObjectCount = layout->getSubObjectSlotCount();
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

        const auto& bindingRangeInfo = layout->getBindingRange(subObjectRangeInfo.bindingRangeIndex);
        for (uint32_t i = 0; i < bindingRangeInfo.count; ++i)
        {
            RefPtr<ShaderObjectImpl> subObject;
            SLANG_RETURN_ON_FAIL(ShaderObjectImpl::create(device, subObjectLayout, subObject.writeRef()));
            m_objects[bindingRangeInfo.subObjectIndex + i] = subObject;
        }
    }

    m_state = State::Initialized;

    return SLANG_OK;
}

/// Write the uniform/ordinary data of this object into the given `dest` buffer at the given
/// `offset`

Result ShaderObjectImpl::_writeOrdinaryData(
    BindingContext& context,
    BufferImpl* buffer,
    Offset offset,
    Size destSize,
    ShaderObjectLayoutImpl* specializedLayout
)
{
    auto src = m_data.getBuffer();
    auto srcSize = Size(m_data.getCount());

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
    for (const auto& subObjectRangeInfo : specializedLayout->getSubObjectRanges())
    {
        Index subObjectRangeIndex = subObjectRangeCounter++;
        const auto& bindingRangeInfo = specializedLayout->getBindingRange(subObjectRangeInfo.bindingRangeIndex);

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

        for (uint32_t i = 0; i < count; ++i)
        {
            auto subObject = m_objects[bindingRangeInfo.subObjectIndex + i];

            RefPtr<ShaderObjectLayoutImpl> subObjectLayout;
            SLANG_RETURN_ON_FAIL(subObject->getSpecializedLayout(subObjectLayout.writeRef()));

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

/// Ensure that the `m_ordinaryDataBuffer` has been created, if it is needed

Result ShaderObjectImpl::_ensureOrdinaryDataBufferCreatedIfNeeded(
    BindingContext& context,
    ShaderObjectLayoutImpl* specializedLayout
)
{
    // If data has been changed since last allocation/filling of constant buffer,
    // we will need to allocate a new one.
    //
    // TODO: for now always allocate constant buffer
    m_isConstantBufferDirty = false;

    // Computing the size of the ordinary data buffer is *not* just as simple
    // as using the size of the `m_ordinayData` array that we store. The reason
    // for the added complexity is that interface-type fields may lead to the
    // storage being specialized such that it needs extra appended data to
    // store the concrete values that logically belong in those interface-type
    // fields but wouldn't fit in the fixed-size allocation we gave them.
    //
    m_constantBufferSize = specializedLayout->getTotalOrdinaryDataSize();
    if (m_constantBufferSize == 0)
    {
        return SLANG_OK;
    }

    // Once we have computed how large the buffer should be, we can allocate
    // it from the transient resource heap.
    //
    auto alignedConstantBufferSize = D3DUtil::calcAligned(m_constantBufferSize, 256);
    SLANG_RETURN_ON_FAIL(
        context.allocateConstantBuffer(alignedConstantBufferSize, m_constantBufferWeakPtr, m_constantBufferOffset)
    );

    // Once the buffer is allocated, we can use `_writeOrdinaryData` to fill it in.
    //
    // Note that `_writeOrdinaryData` is potentially recursive in the case
    // where this object contains interface/existential-type fields, so we
    // don't need or want to inline it into this call site.
    //
    SLANG_RETURN_ON_FAIL(_writeOrdinaryData(
        context,
        m_constantBufferWeakPtr,
        m_constantBufferOffset,
        m_constantBufferSize,
        specializedLayout
    ));

    {
        // We also create and store a descriptor for our root constant buffer
        // into the descriptor table allocation that was reserved for them.
        //
        // We always know that the ordinary data buffer will be the first descriptor
        // in the table of resource views.
        //
        auto descriptorTable = m_descriptorSet.resourceTable;
        context.submitter->createConstantBufferView(
            m_constantBufferWeakPtr->getDeviceAddress() + m_constantBufferOffset,
            alignedConstantBufferSize,
            descriptorTable.getCpuHandle()
        );
    }

    return SLANG_OK;
}

static void bindPendingTables(BindingContext& context)
{
    for (auto& binding : *context.pendingTableBindings)
    {
        context.submitter->setRootDescriptorTable(binding.rootIndex, binding.handle);
    }
}

/// Prepare to bind this object as a parameter block.
///
/// This involves allocating and binding any descriptor tables necessary
/// to to store the state of the object. The function returns a descriptor
/// set formed from any table(s) allocated. In addition, the `ioOffset`
/// parameter will be adjusted to be correct for binding values into
/// the resulting descriptor set.
///
/// Returns:
///   SLANG_OK when successful,
///   SLANG_E_OUT_OF_MEMORY when descriptor heap is full.
///

Result ShaderObjectImpl::prepareToBindAsParameterBlock(
    BindingContext& context,
    BindingOffset& ioOffset,
    ShaderObjectLayoutImpl* specializedLayout,
    DescriptorSet& outDescriptorSet
)
{
    auto submitter = context.submitter;

    // When writing into the new descriptor set, resource and sampler
    // descriptors will need to start at index zero in the respective
    // tables.
    //
    ioOffset.resource = 0;
    ioOffset.sampler = 0;

    // The index of the next root parameter to bind will be maintained,
    // but needs to be incremented by the number of descriptor tables
    // we allocate (zero or one resource table and zero or one sampler
    // table).
    //
    auto& rootParamIndex = ioOffset.rootParam;

    if (auto descriptorCount = specializedLayout->getTotalResourceDescriptorCount())
    {
        // There is a non-zero number of resource descriptors needed,
        // so we will allocate a table out of the appropriate heap,
        // and store it into the appropriate part of `descriptorSet`.
        //
        auto descriptorHeap = context.viewHeap;
        auto& table = outDescriptorSet.resourceTable;

        // Allocate the table.
        //
        if (!table.allocate(descriptorHeap, descriptorCount))
        {
            context.outOfMemoryHeap = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            return SLANG_E_OUT_OF_MEMORY;
        }

        // Bind the table to the pipeline, consuming the next available
        // root parameter.
        //
        auto tableRootParamIndex = rootParamIndex++;
        context.pendingTableBindings->push_back(PendingDescriptorTableBinding{tableRootParamIndex, table.getGpuHandle()}
        );
    }
    if (auto descriptorCount = specializedLayout->getTotalSamplerDescriptorCount())
    {
        // There is a non-zero number of sampler descriptors needed,
        // so we will allocate a table out of the appropriate heap,
        // and store it into the appropriate part of `descriptorSet`.
        //
        auto descriptorHeap = context.samplerHeap;
        auto& table = outDescriptorSet.samplerTable;

        // Allocate the table.
        //
        if (!table.allocate(descriptorHeap, descriptorCount))
        {
            context.outOfMemoryHeap = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
            return SLANG_E_OUT_OF_MEMORY;
        }

        // Bind the table to the pipeline, consuming the next available
        // root parameter.
        //
        auto tableRootParamIndex = rootParamIndex++;
        context.pendingTableBindings->push_back(PendingDescriptorTableBinding{tableRootParamIndex, table.getGpuHandle()}
        );
    }

    return SLANG_OK;
}

/// Bind this object as a `ParameterBlock<X>`

Result ShaderObjectImpl::bindAsParameterBlock(
    BindingContext& context,
    const BindingOffset& offset,
    ShaderObjectLayoutImpl* specializedLayout
)
{
    // The first step to binding an object as a parameter block is to allocate a descriptor
    // set (consisting of zero or one resource descriptor table and zero or one sampler
    // descriptor table) to represent its values.
    //
    BindingOffset subOffset = offset;
    short_vector<PendingDescriptorTableBinding> pendingTableBindings;
    auto oldPendingTableBindings = context.pendingTableBindings;
    context.pendingTableBindings = &pendingTableBindings;

    SLANG_RETURN_ON_FAIL(
        prepareToBindAsParameterBlock(context, /* inout */ subOffset, specializedLayout, m_cachedGPUDescriptorSet)
    );

    // Next we bind the object into that descriptor set as if it were being used
    // as a `ConstantBuffer<X>`.
    //
    SLANG_RETURN_ON_FAIL(bindAsConstantBuffer(context, m_cachedGPUDescriptorSet, subOffset, specializedLayout));

    bindPendingTables(context);
    context.pendingTableBindings = oldPendingTableBindings;

    return SLANG_OK;
}

/// Bind this object as a `ConstantBuffer<X>`

Result ShaderObjectImpl::bindAsConstantBuffer(
    BindingContext& context,
    const DescriptorSet& descriptorSet,
    const BindingOffset& offset,
    ShaderObjectLayoutImpl* specializedLayout
)
{
    // If we are to bind as a constant buffer we first need to ensure that
    // the ordinary data buffer is created, if this object needs one.
    //
    SLANG_RETURN_ON_FAIL(_ensureOrdinaryDataBufferCreatedIfNeeded(context, specializedLayout));

    // Next, we need to bind all of the resource descriptors for this object
    // (including any ordinary data buffer) into the provided `descriptorSet`.
    //
    auto resourceCount = specializedLayout->getResourceSlotCount();
    if (resourceCount)
    {
        auto& dstTable = descriptorSet.resourceTable;
        auto& srcTable = m_descriptorSet.resourceTable;

        context.submitter->copyDescriptors(
            UINT(resourceCount),
            dstTable.getCpuHandle(offset.resource),
            srcTable.getCpuHandle(),
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
        );
    }

    // Finally, we delegate to `_bindImpl` to bind samplers and sub-objects,
    // since the logic is shared with the `bindAsValue()` case below.
    //
    SLANG_RETURN_ON_FAIL(_bindImpl(context, descriptorSet, offset, specializedLayout));
    return SLANG_OK;
}

/// Bind this object as a value (for an interface-type parameter)

Result ShaderObjectImpl::bindAsValue(
    BindingContext& context,
    const DescriptorSet& descriptorSet,
    const BindingOffset& offset,
    ShaderObjectLayoutImpl* specializedLayout
)
{
    // When binding a value for an interface-type field we do *not* want
    // to bind a buffer for the ordinary data (if there is any) because
    // ordinary data for interface-type fields gets allocated into the
    // parent object's ordinary data buffer.
    //
    // This CPU-memory descriptor table that holds resource descriptors
    // will have already been allocated to have space for an ordinary data
    // buffer (if needed), so we need to take care to skip over that
    // descriptor when copying descriptors from the CPU-memory set
    // to the GPU-memory `descriptorSet`.
    //
    auto skipResourceCount = specializedLayout->getOrdinaryDataBufferCount();
    auto resourceCount = specializedLayout->getResourceSlotCount() - skipResourceCount;
    if (resourceCount)
    {
        auto& dstTable = descriptorSet.resourceTable;
        auto& srcTable = m_descriptorSet.resourceTable;

        context.submitter->copyDescriptors(
            UINT(resourceCount),
            dstTable.getCpuHandle(offset.resource),
            srcTable.getCpuHandle(skipResourceCount),
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
        );
    }

    // Finally, we delegate to `_bindImpl` to bind samplers and sub-objects,
    // since the logic is shared with the `bindAsConstantBuffer()` case above.
    //
    // Note: Just like we had to do some subtle handling of the ordinary data buffer
    // above, here we need to contend with the fact that the `offset.resource` fields
    // computed for sub-object ranges were baked to take the ordinary data buffer
    // into account, so that if `skipResourceCount` is non-zero then they are all
    // too high by `skipResourceCount`.
    //
    // We will address the problem here by computing a modified offset that adjusts
    // for the ordinary data buffer that we have not bound after all.
    //
    BindingOffset subOffset = offset;
    subOffset.resource -= skipResourceCount;
    SLANG_RETURN_ON_FAIL(_bindImpl(context, descriptorSet, subOffset, specializedLayout));
    return SLANG_OK;
}

/// Shared logic for `bindAsConstantBuffer()` and `bindAsValue()`

Result ShaderObjectImpl::_bindImpl(
    BindingContext& context,
    const DescriptorSet& descriptorSet,
    const BindingOffset& offset,
    ShaderObjectLayoutImpl* specializedLayout
)
{
    // We start by binding all the sampler decriptors, if needed.
    //
    // Note: resource descriptors were handled in either `bindAsConstantBuffer()`
    // or `bindAsValue()` before calling into `_bindImpl()`.
    //
    if (auto samplerCount = specializedLayout->getSamplerSlotCount())
    {
        auto& dstTable = descriptorSet.samplerTable;
        auto& srcTable = m_descriptorSet.samplerTable;

        context.submitter->copyDescriptors(
            UINT(samplerCount),
            dstTable.getCpuHandle(offset.sampler),
            srcTable.getCpuHandle(),
            D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER
        );
    }

    // Next we iterate over the sub-object ranges and bind anything they require.
    //
    auto& subObjectRanges = specializedLayout->getSubObjectRanges();
    auto subObjectRangeCount = subObjectRanges.size();
    for (Index i = 0; i < subObjectRangeCount; i++)
    {
        auto& subObjectRange = specializedLayout->getSubObjectRange(i);
        const auto& bindingRange = specializedLayout->getBindingRange(subObjectRange.bindingRangeIndex);
        auto subObjectIndex = bindingRange.subObjectIndex;
        auto subObjectLayout = subObjectRange.layout.get();

        BindingOffset rangeOffset = offset;
        rangeOffset += subObjectRange.offset;

        BindingOffset rangeStride = subObjectRange.stride;

        switch (bindingRange.bindingType)
        {
        case slang::BindingType::ConstantBuffer:
        {
            auto objOffset = rangeOffset;
            for (uint32_t j = 0; j < bindingRange.count; j++)
            {
                auto& object = m_objects[subObjectIndex + j];
                SLANG_RETURN_ON_FAIL(object->bindAsConstantBuffer(context, descriptorSet, objOffset, subObjectLayout));
                objOffset += rangeStride;
            }
        }
        break;

        case slang::BindingType::ParameterBlock:
        {
            auto objOffset = rangeOffset;
            for (uint32_t j = 0; j < bindingRange.count; j++)
            {
                auto& object = m_objects[subObjectIndex + j];
                SLANG_RETURN_ON_FAIL(object->bindAsParameterBlock(context, objOffset, subObjectLayout));
                objOffset += rangeStride;
            }
        }
        break;

        case slang::BindingType::ExistentialValue:
            if (subObjectLayout)
            {
                auto objOffset = rangeOffset;
                for (uint32_t j = 0; j < bindingRange.count; j++)
                {
                    auto& object = m_objects[subObjectIndex + j];
                    SLANG_RETURN_ON_FAIL(object->bindAsValue(context, descriptorSet, objOffset, subObjectLayout));
                    objOffset += rangeStride;
                }
            }
            break;
        }
    }

    return SLANG_OK;
}

Result ShaderObjectImpl::bindRootArguments(BindingContext& context, uint32_t& index)
{
    auto layoutImpl = getLayout();
    for (Index i = 0; i < m_rootArguments.size(); i++)
    {
        if (layoutImpl->getRootParameterInfo(i).isUAV)
        {
            context.submitter->setRootUAV(index, m_rootArguments[i]);
        }
        else
        {
            context.submitter->setRootSRV(index, m_rootArguments[i]);
        }
        index++;
    }
    for (auto& subObject : m_objects)
    {
        if (subObject)
        {
            SLANG_RETURN_ON_FAIL(subObject->bindRootArguments(context, index));
        }
    }
    return SLANG_OK;
}

void ShaderObjectImpl::setResourceStates(StateTracking& stateTracking)
{
    for (const BoundResource& boundResource : m_boundResources)
    {
        switch (boundResource.type)
        {
        case BoundResourceType::Buffer:
            stateTracking.setBufferState(
                checked_cast<BufferImpl*>(boundResource.resource.get()),
                boundResource.requiredState
            );
            break;
        case BoundResourceType::TextureView:
        {
            TextureViewImpl* textureView = checked_cast<TextureViewImpl*>(boundResource.resource.get());
            stateTracking.setTextureState(
                textureView->m_texture,
                textureView->m_desc.subresourceRange,
                boundResource.requiredState
            );
            break;
        }
        case BoundResourceType::AccelerationStructure:
            // TODO STATE_TRACKING need state transition?
            break;
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

/// Get the layout of this shader object with specialization arguments considered
///
/// This operation should only be called after the shader object has been
/// fully filled in and finalized.
///

Result ShaderObjectImpl::getSpecializedLayout(ShaderObjectLayoutImpl** outLayout)
{
    if (!m_specializedLayout)
    {
        SLANG_RETURN_ON_FAIL(_createSpecializedLayout(m_specializedLayout.writeRef()));
    }
    returnRefPtr(outLayout, m_specializedLayout);
    return SLANG_OK;
}

/// Create the layout for this shader object with specialization arguments considered
///
/// This operation is virtual so that it can be customized by `RootShaderObject`.
///

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

Result ShaderObjectImpl::setBinding(const ShaderOffset& offset, Binding binding)
{
    SLANG_RETURN_ON_FAIL(requireNotFinalized());

    if (offset.bindingRangeIndex < 0)
        return SLANG_E_INVALID_ARG;
    auto layout = getLayout();
    if (offset.bindingRangeIndex >= layout->getBindingRangeCount())
        return SLANG_E_INVALID_ARG;

    ID3D12Device* d3dDevice = checked_cast<DeviceImpl*>(getDevice())->m_device;

    auto& bindingRange = layout->getBindingRange(offset.bindingRangeIndex);

    Index bindingIndex = bindingRange.baseIndex + offset.bindingArrayIndex;

    BoundResource& boundResource = m_boundResources[bindingIndex];

    switch (binding.type)
    {
    case BindingType::Buffer:
    case BindingType::BufferWithCounter:
    {
        BufferImpl* buffer = checked_cast<BufferImpl*>(binding.resource);
        BufferImpl* counterBuffer = checked_cast<BufferImpl*>(binding.resource2);
        BufferRange bufferRange = buffer->resolveBufferRange(binding.bufferRange);
        boundResource.type = BoundResourceType::Buffer;
        boundResource.resource = buffer;
        boundResource.counterResource = counterBuffer;
        if (bindingRange.isRootParameter)
        {
            m_rootArguments[bindingRange.baseIndex] = buffer->getDeviceAddress() + bufferRange.offset;
        }
        else
        {
            D3D12Descriptor descriptor;
            switch (bindingRange.bindingType)
            {
            case slang::BindingType::TypedBuffer:
                descriptor = buffer->getSRV(buffer->m_desc.format, 0, bufferRange);
                boundResource.requiredState = ResourceState::ShaderResource;
                break;
            case slang::BindingType::RawBuffer:
                descriptor = buffer->getSRV(Format::Unknown, bindingRange.bufferElementStride, bufferRange);
                boundResource.requiredState = ResourceState::ShaderResource;
                break;
            case slang::BindingType::MutableTypedBuffer:
                descriptor = buffer->getUAV(buffer->m_desc.format, 0, bufferRange);
                boundResource.requiredState = ResourceState::UnorderedAccess;
                break;
            case slang::BindingType::MutableRawBuffer:
                descriptor =
                    buffer->getUAV(Format::Unknown, bindingRange.bufferElementStride, bufferRange, counterBuffer);
                boundResource.requiredState = ResourceState::UnorderedAccess;
                break;
            default:
                return SLANG_FAIL;
            }
            d3dDevice->CopyDescriptorsSimple(
                1,
                m_descriptorSet.resourceTable.getCpuHandle(bindingIndex),
                descriptor.cpuHandle,
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
            );
        }
        break;
    }
    case BindingType::Texture:
    {
        TextureImpl* texture = checked_cast<TextureImpl*>(binding.resource);
        return setBinding(offset, m_device->createTextureView(texture, {}));
    }
    case BindingType::TextureView:
    {
        TextureViewImpl* textureView = checked_cast<TextureViewImpl*>(binding.resource);
        boundResource.type = BoundResourceType::TextureView;
        boundResource.resource = textureView;
        D3D12Descriptor descriptor;
        switch (bindingRange.bindingType)
        {
        case slang::BindingType::Texture:
            descriptor = textureView->getSRV();
            boundResource.requiredState = ResourceState::ShaderResource;
            break;
        case slang::BindingType::MutableTexture:
            descriptor = textureView->getUAV();
            boundResource.requiredState = ResourceState::UnorderedAccess;
            break;
        default:
            return SLANG_FAIL;
        }
        d3dDevice->CopyDescriptorsSimple(
            1,
            m_descriptorSet.resourceTable.getCpuHandle(bindingIndex),
            descriptor.cpuHandle,
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
        );
        break;
    }
    case BindingType::Sampler:
    {
        SamplerImpl* sampler = checked_cast<SamplerImpl*>(binding.resource);
        d3dDevice->CopyDescriptorsSimple(
            1,
            m_descriptorSet.samplerTable.getCpuHandle(bindingIndex),
            sampler->m_descriptor.cpuHandle,
            D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER
        );
        break;
    }
    case BindingType::CombinedTextureSampler:
    {
        TextureImpl* texture = checked_cast<TextureImpl*>(binding.resource);
        SamplerImpl* sampler = checked_cast<SamplerImpl*>(binding.resource2);
        return setBinding(offset, Binding(m_device->createTextureView(texture, {}), sampler));
    }
    case BindingType::CombinedTextureViewSampler:
    {
        TextureViewImpl* textureView = checked_cast<TextureViewImpl*>(binding.resource);
        SamplerImpl* sampler = checked_cast<SamplerImpl*>(binding.resource2);
        boundResource.type = BoundResourceType::TextureView;
        boundResource.resource = textureView;
        boundResource.requiredState = ResourceState::ShaderResource;
        d3dDevice->CopyDescriptorsSimple(
            1,
            m_descriptorSet.resourceTable.getCpuHandle(bindingIndex),
            textureView->getSRV().cpuHandle,
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
        );
        d3dDevice->CopyDescriptorsSimple(
            1,
            m_descriptorSet.samplerTable.getCpuHandle(bindingIndex),
            sampler->m_descriptor.cpuHandle,
            D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER
        );
        break;
    }
    case BindingType::AccelerationStructure:
    {
        AccelerationStructureImpl* as = checked_cast<AccelerationStructureImpl*>(binding.resource);
        boundResource.type = BoundResourceType::AccelerationStructure;
        boundResource.resource = as;
        if (bindingRange.isRootParameter)
        {
            m_rootArguments[bindingRange.baseIndex] = as->getDeviceAddress();
        }
        else
        {
            d3dDevice->CopyDescriptorsSimple(
                1,
                m_descriptorSet.resourceTable.getCpuHandle(bindingIndex),
                as->m_descriptor.cpuHandle,
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
            );
        }
        break;
    }
    }

    return SLANG_OK;
}

Result ShaderObjectImpl::create(DeviceImpl* device, ShaderObjectLayoutImpl* layout, ShaderObjectImpl** outShaderObject)
{
    auto object = RefPtr<ShaderObjectImpl>(new ShaderObjectImpl());
    SLANG_RETURN_ON_FAIL(object->init(device, layout, device->m_cpuViewHeap.get(), device->m_cpuSamplerHeap.get()));
    returnRefPtrMove(outShaderObject, object);
    return SLANG_OK;
}

ShaderObjectImpl::~ShaderObjectImpl()
{
    m_descriptorSet.freeIfSupported();
}

RootShaderObjectLayoutImpl* RootShaderObjectImpl::getLayout()
{
    return checked_cast<RootShaderObjectLayoutImpl*>(m_layout.get());
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

Result RootShaderObjectImpl::collectSpecializationArgs(ExtendedShaderObjectTypeList& args)
{
    SLANG_RETURN_ON_FAIL(ShaderObjectImpl::collectSpecializationArgs(args));
    for (auto& entryPoint : m_entryPoints)
    {
        SLANG_RETURN_ON_FAIL(entryPoint->collectSpecializationArgs(args));
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

    if (diagnosticBlob && diagnosticBlob->getBufferSize())
    {
        m_device->handleMessage(
            SLANG_FAILED(result) ? DebugMessageType::Error : DebugMessageType::Info,
            DebugMessageSource::Layer,
            (const char*)diagnosticBlob->getBufferPointer()
        );
    }

    if (SLANG_FAILED(result))
        return result;

    ComPtr<ID3DBlob> d3dDiagnosticBlob;
    auto slangSpecializedLayout = specializedComponentType->getLayout();
    RefPtr<RootShaderObjectLayoutImpl> specializedLayout;
    auto rootLayoutResult = RootShaderObjectLayoutImpl::create(
        checked_cast<DeviceImpl*>(getDevice()),
        specializedComponentType,
        slangSpecializedLayout,
        specializedLayout.writeRef(),
        d3dDiagnosticBlob.writeRef()
    );

    if (SLANG_FAILED(rootLayoutResult))
    {
        return rootLayoutResult;
    }

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

void RootShaderObjectImpl::setResourceStates(StateTracking& stateTracking)
{
    ShaderObjectImpl::setResourceStates(stateTracking);
    for (auto& entryPoint : m_entryPoints)
    {
        entryPoint->setResourceStates(stateTracking);
    }
}

Result RootShaderObjectImpl::bindAsRoot(BindingContext& context, RootShaderObjectLayoutImpl* specializedLayout)
{
    // A root shader object always binds as if it were a parameter block,
    // insofar as it needs to allocate a descriptor set to hold the bindings
    // for its own state and any sub-objects.
    //
    // Note: We do not direclty use `bindAsParameterBlock` here because we also
    // need to bind the entry points into the same descriptor set that is
    // being used for the root object.

    short_vector<PendingDescriptorTableBinding> pendingTableBindings;
    auto oldPendingTableBindings = context.pendingTableBindings;
    context.pendingTableBindings = &pendingTableBindings;

    BindingOffset rootOffset;

    // Bind all root parameters first.
    Super::bindRootArguments(context, rootOffset.rootParam);

    DescriptorSet descriptorSet;
    SLANG_RETURN_ON_FAIL(
        prepareToBindAsParameterBlock(context, /* inout */ rootOffset, specializedLayout, descriptorSet)
    );

    SLANG_RETURN_ON_FAIL(Super::bindAsConstantBuffer(context, descriptorSet, rootOffset, specializedLayout));

    auto entryPointCount = m_entryPoints.size();
    for (Index i = 0; i < entryPointCount; ++i)
    {
        auto entryPoint = m_entryPoints[i];
        auto& entryPointInfo = specializedLayout->getEntryPoint(i);

        auto entryPointOffset = rootOffset;
        entryPointOffset += entryPointInfo.offset;

        SLANG_RETURN_ON_FAIL(
            entryPoint->bindAsConstantBuffer(context, descriptorSet, entryPointOffset, entryPointInfo.layout)
        );
    }

    bindPendingTables(context);
    context.pendingTableBindings = oldPendingTableBindings;

    return SLANG_OK;
}

Result RootShaderObjectImpl::init(DeviceImpl* device, RootShaderObjectLayoutImpl* layout)
{
    SLANG_RETURN_ON_FAIL(Super::init(device, layout, device->m_cpuViewHeap.get(), device->m_cpuSamplerHeap.get()));
    for (auto entryPointInfo : layout->getEntryPoints())
    {
        RefPtr<ShaderObjectImpl> entryPoint;
        SLANG_RETURN_ON_FAIL(ShaderObjectImpl::create(device, entryPointInfo.layout, entryPoint.writeRef()));
        m_entryPoints.push_back(entryPoint);
    }
    return SLANG_OK;
}

} // namespace rhi::d3d12
