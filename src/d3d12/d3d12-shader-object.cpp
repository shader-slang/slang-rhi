#include "d3d12-shader-object.h"
#include "d3d12-buffer.h"
#include "d3d12-texture.h"
#include "d3d12-device.h"
#include "d3d12-utils.h"
#include "d3d12-sampler.h"
#include "d3d12-shader-object-layout.h"
#include "d3d12-acceleration-structure.h"

namespace rhi::d3d12 {

inline BindingDataImpl::RootParameter createRootDescriptorTable(UINT index, D3D12_GPU_DESCRIPTOR_HANDLE baseDescriptor)
{
    BindingDataImpl::RootParameter result;
    result.type = BindingDataImpl::RootParameter::DescriptorTable;
    result.index = index;
    result.baseDescriptor = baseDescriptor;
    return result;
}

inline BindingDataImpl::RootParameter createRootSRV(UINT index, D3D12_GPU_VIRTUAL_ADDRESS bufferLocation)
{
    BindingDataImpl::RootParameter result;
    result.type = BindingDataImpl::RootParameter::SRV;
    result.index = index;
    result.bufferLocation = bufferLocation;
    return result;
}

inline BindingDataImpl::RootParameter createRootUAV(UINT index, D3D12_GPU_VIRTUAL_ADDRESS bufferLocation)
{
    BindingDataImpl::RootParameter result;
    result.type = BindingDataImpl::RootParameter::UAV;
    result.index = index;
    result.bufferLocation = bufferLocation;
    return result;
}

inline void writeBufferState(BindingDataBuilder* builder, BufferImpl* buffer, ResourceState state)
{
    BindingDataImpl* bindingData = builder->m_bindingData;
    if (bindingData->bufferStateCount >= bindingData->bufferStateCapacity)
    {
        bindingData->bufferStateCapacity *= 2;
        BindingDataImpl::BufferState* newBufferStates =
            builder->m_allocator->allocate<BindingDataImpl::BufferState>(bindingData->bufferStateCapacity);
        std::memcpy(
            newBufferStates,
            bindingData->bufferStates,
            bindingData->bufferStateCount * sizeof(BindingDataImpl::BufferState)
        );
        bindingData->bufferStates = newBufferStates;
    }
    bindingData->bufferStates[bindingData->bufferStateCount++] = {buffer, state};
}

inline void writeTextureState(BindingDataBuilder* builder, TextureViewImpl* textureView, ResourceState state)
{
    BindingDataImpl* bindingData = builder->m_bindingData;
    if (bindingData->textureStateCount >= bindingData->textureStateCapacity)
    {
        bindingData->textureStateCapacity *= 2;
        BindingDataImpl::TextureState* newTextureStates =
            builder->m_allocator->allocate<BindingDataImpl::TextureState>(bindingData->textureStateCapacity);
        std::memcpy(
            newTextureStates,
            bindingData->textureStates,
            bindingData->textureStateCount * sizeof(BindingDataImpl::TextureState)
        );
        bindingData->textureStates = newTextureStates;
    }
    bindingData->textureStates[bindingData->textureStateCount++] = {textureView, state};
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

    // TODO(shaderobject): allocate actual number of buffer/texture resources
    // For now we use a fixed starting capacity and grow as needed.
    m_bindingData->bufferStateCapacity = 1024;
    m_bindingData->bufferStates =
        m_allocator->allocate<BindingDataImpl::BufferState>(m_bindingData->bufferStateCapacity);
    m_bindingData->bufferStateCount = 0;
    m_bindingData->textureStateCapacity = 1024;
    m_bindingData->textureStates =
        m_allocator->allocate<BindingDataImpl::TextureState>(m_bindingData->textureStateCapacity);
    m_bindingData->textureStateCount = 0;

    uint32_t rootParameterCount = specializedLayout->m_rootSignatureTotalParameterCount;
    m_bindingData->rootParameters = m_allocator->allocate<BindingDataImpl::RootParameter>(rootParameterCount);
    m_bindingData->rootParameterCount = rootParameterCount;

    // A root shader object always binds as if it were a parameter block,
    // insofar as it needs to allocate a descriptor set to hold the bindings
    // for its own state and any sub-objects.
    //
    // Note: We do not direclty use `bindAsParameterBlock` here because we also
    // need to bind the entry points into the same descriptor set that is
    // being used for the root object.

    BindingOffset rootOffset;

    uint32_t rootParamIndex = 0;
    rootOffset.rootParam += specializedLayout->m_rootSignatureRootParameterCount;

    DescriptorSet descriptorSet;
    SLANG_RETURN_ON_FAIL(
        allocateDescriptorSets(shaderObject, /* inout */ rootOffset, specializedLayout, descriptorSet)
    );

    SLANG_RETURN_ON_FAIL(
        bindAsConstantBuffer(shaderObject, descriptorSet, rootOffset, rootParamIndex, specializedLayout)
    );

    size_t entryPointCount = specializedLayout->m_entryPoints.size();
    for (size_t i = 0; i < entryPointCount; ++i)
    {
        auto entryPoint = shaderObject->m_entryPoints[i];
        const auto& entryPointInfo = specializedLayout->m_entryPoints[i];
        ShaderObjectLayoutImpl* entryPointLayout = entryPointInfo.layout;

        auto entryPointOffset = rootOffset;
        entryPointOffset += entryPointInfo.offset;

        SLANG_RETURN_ON_FAIL(
            bindAsConstantBuffer(entryPoint, descriptorSet, entryPointOffset, rootParamIndex, entryPointLayout)
        );
    }

#if SLANG_RHI_ENABLE_NVAPI
    // Bind null-descriptor to implicit descriptor range for the NVAPI UAV descriptor if needed.
    if (specializedLayout->m_hasImplicitDescriptorRangeForNVAPI)
    {
        SLANG_RHI_ASSERT(descriptorSet.resources.count > 0);

        // The NVAPI UAV descriptor should be at the very last position in the descriptor table
        // since we added the NVAPI UAV range last during root signature creation.
        uint32_t nvapiDescriptorIndex = descriptorSet.resources.count - 1;

        D3D12_CPU_DESCRIPTOR_HANDLE nullUavDescriptor =
            m_device->getNullDescriptor(slang::BindingType::MutableRawBuffer, SLANG_STRUCTURED_BUFFER);

        m_device->m_device->CopyDescriptorsSimple(
            1,
            descriptorSet.resources.getCpuHandle(nvapiDescriptorIndex),
            nullUavDescriptor,
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
        );
    }
#endif

    outBindingData = bindingData;

    return SLANG_OK;
}

Result BindingDataBuilder::allocateDescriptorSets(
    ShaderObject* shaderObject,
    BindingOffset& ioOffset,
    ShaderObjectLayoutImpl* specializedLayout,
    DescriptorSet& outDescriptorSet
)
{
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

    if (uint32_t descriptorCount = specializedLayout->getTotalResourceDescriptorCount())
    {
        auto allocation = m_cbvSrvUavArena->allocate(descriptorCount);
        if (!allocation.isValid())
        {
            return SLANG_E_OUT_OF_MEMORY;
        }
        SLANG_RHI_ASSERT(rootParamIndex < m_bindingData->rootParameterCount);
        m_bindingData->rootParameters[rootParamIndex++] =
            createRootDescriptorTable(rootParamIndex, allocation.firstGpuHandle);
        outDescriptorSet.resources = allocation;
    }
    if (auto descriptorCount = specializedLayout->getTotalSamplerDescriptorCount())
    {
        auto allocation = m_samplerArena->allocate(descriptorCount);
        if (!allocation.isValid())
        {
            return SLANG_E_OUT_OF_MEMORY;
        }
        SLANG_RHI_ASSERT(rootParamIndex < m_bindingData->rootParameterCount);
        m_bindingData->rootParameters[rootParamIndex++] =
            createRootDescriptorTable(rootParamIndex, allocation.firstGpuHandle);
        outDescriptorSet.samplers = allocation;
    }

    return SLANG_OK;
}

/// Bind this object as a `ConstantBuffer<X>`
Result BindingDataBuilder::bindAsConstantBuffer(
    ShaderObject* shaderObject,
    const DescriptorSet& descriptorSet,
    const BindingOffset& inOffset,
    uint32_t& rootParamIndex,
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
    SLANG_RETURN_ON_FAIL(
        bindOrdinaryDataBufferIfNeeded(shaderObject, descriptorSet, /*inout*/ offset, specializedLayout)
    );
    SLANG_RETURN_ON_FAIL(bindAsValue(shaderObject, descriptorSet, inOffset, rootParamIndex, specializedLayout));
    return SLANG_OK;
}

/// Bind this object as a `ParameterBlock<X>`
Result BindingDataBuilder::bindAsParameterBlock(
    ShaderObject* shaderObject,
    const BindingOffset& offset,
    uint32_t& rootParamIndex,
    ShaderObjectLayoutImpl* specializedLayout
)
{
    // The first step to binding an object as a parameter block is to allocate a descriptor
    // set (consisting of zero or one resource descriptor table and zero or one sampler
    // descriptor table) to represent its values.
    //
    BindingOffset subOffset = offset;

    DescriptorSet descriptorSet;

    SLANG_RETURN_ON_FAIL(allocateDescriptorSets(shaderObject, /* inout */ subOffset, specializedLayout, descriptorSet));

    // Next we bind the object into that descriptor set as if it were being used
    // as a `ConstantBuffer<X>`.
    //
    SLANG_RETURN_ON_FAIL(
        bindAsConstantBuffer(shaderObject, descriptorSet, subOffset, rootParamIndex, specializedLayout)
    );

    return SLANG_OK;
}

Result BindingDataBuilder::bindAsValue(
    ShaderObject* shaderObject,
    const DescriptorSet& descriptorSet,
    const BindingOffset& offset,
    uint32_t& rootParamIndex,
    ShaderObjectLayoutImpl* specializedLayout
)
{
    ID3D12Device* d3dDevice = m_device->m_device;

    // We start by iterating over the "simple" (non-sub-object) binding
    // ranges and writing them to the descriptor sets that are being
    // passed down.
    //
    for (const auto& bindingRangeInfo : specializedLayout->m_bindingRanges)
    {
        BindingOffset rangeOffset = offset;

        uint32_t slotIndex = bindingRangeInfo.slotIndex;
        uint32_t baseIndex = bindingRangeInfo.baseIndex;
        uint32_t count = bindingRangeInfo.count;

        switch (bindingRangeInfo.bindingType)
        {
        case slang::BindingType::ConstantBuffer:
        case slang::BindingType::ParameterBlock:
        case slang::BindingType::ExistentialValue:
            break;

        case slang::BindingType::Texture:
        case slang::BindingType::MutableTexture:
        {
            uint32_t resourceIndex = rangeOffset.resource + baseIndex;
            bool isSrv = bindingRangeInfo.bindingType == slang::BindingType::Texture;
            ResourceState requiredState = isSrv ? ResourceState::ShaderResource : ResourceState::UnorderedAccess;
            for (uint32_t i = 0; i < count; ++i)
            {
                const ResourceSlot& slot = shaderObject->m_slots[slotIndex + i];
                TextureViewImpl* textureView = checked_cast<TextureViewImpl*>(slot.resource.get());
                D3D12_CPU_DESCRIPTOR_HANDLE descriptor = {};
                if (textureView)
                {
                    SLANG_RHI_ASSERT(resourceIndex + i < descriptorSet.resources.count);
                    descriptor = isSrv ? textureView->getSRV() : textureView->getUAV();
                    writeTextureState(this, textureView, requiredState);
                }
                else
                {
                    descriptor =
                        m_device->getNullDescriptor(bindingRangeInfo.bindingType, bindingRangeInfo.resourceShape);
                }
                d3dDevice->CopyDescriptorsSimple(
                    1,
                    descriptorSet.resources.getCpuHandle(resourceIndex + i),
                    descriptor,
                    D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
                );
            }
            break;
        }
        case slang::BindingType::CombinedTextureSampler:
        {
            uint32_t resourceIndex = rangeOffset.resource + baseIndex;
            uint32_t samplerIndex = rangeOffset.sampler + baseIndex;
            for (uint32_t i = 0; i < count; ++i)
            {
                const ResourceSlot& slot = shaderObject->m_slots[slotIndex + i];
                TextureViewImpl* textureView = checked_cast<TextureViewImpl*>(slot.resource.get());
                SamplerImpl* sampler = checked_cast<SamplerImpl*>(slot.resource2.get());
                SLANG_RHI_ASSERT(resourceIndex + i < descriptorSet.resources.count);
                D3D12_CPU_DESCRIPTOR_HANDLE textureDescriptor = {};
                if (textureView)
                {
                    textureDescriptor = textureView->getSRV();
                    writeTextureState(this, textureView, ResourceState::UnorderedAccess);
                }
                else
                {
                    textureDescriptor =
                        m_device->getNullDescriptor(slang::BindingType::Texture, bindingRangeInfo.resourceShape);
                }
                d3dDevice->CopyDescriptorsSimple(
                    1,
                    descriptorSet.resources.getCpuHandle(resourceIndex + i),
                    textureDescriptor,
                    D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
                );
                SLANG_RHI_ASSERT(samplerIndex + i < descriptorSet.samplers.count);
                D3D12_CPU_DESCRIPTOR_HANDLE samplerDescriptor = {};
                if (sampler)
                {
                    samplerDescriptor = sampler->m_descriptor.cpuHandle;
                }
                else
                {
                    samplerDescriptor = m_device->getNullSamplerDescriptor();
                }
                d3dDevice->CopyDescriptorsSimple(
                    1,
                    descriptorSet.samplers.getCpuHandle(samplerIndex + i),
                    samplerDescriptor,
                    D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER
                );
            }
            break;
        }
        case slang::BindingType::Sampler:
        {
            uint32_t samplerIndex = rangeOffset.sampler + baseIndex;
            for (uint32_t i = 0; i < count; ++i)
            {
                const ResourceSlot& slot = shaderObject->m_slots[slotIndex + i];
                SamplerImpl* sampler = checked_cast<SamplerImpl*>(slot.resource.get());
                SLANG_RHI_ASSERT(samplerIndex + i < descriptorSet.samplers.count);
                D3D12_CPU_DESCRIPTOR_HANDLE descriptor = {};
                if (sampler)
                {
                    descriptor = sampler->m_descriptor.cpuHandle;
                }
                else
                {
                    descriptor = m_device->getNullSamplerDescriptor();
                }
                d3dDevice->CopyDescriptorsSimple(
                    1,
                    descriptorSet.samplers.getCpuHandle(samplerIndex + i),
                    descriptor,
                    D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER
                );
            }
            break;
        }
        case slang::BindingType::RawBuffer:
        case slang::BindingType::TypedBuffer:
        case slang::BindingType::MutableRawBuffer:
        case slang::BindingType::MutableTypedBuffer:
        {
            bool isSrv = bindingRangeInfo.bindingType == slang::BindingType::RawBuffer ||
                         bindingRangeInfo.bindingType == slang::BindingType::TypedBuffer;
            bool isTyped = bindingRangeInfo.bindingType == slang::BindingType::TypedBuffer ||
                           bindingRangeInfo.bindingType == slang::BindingType::MutableTypedBuffer;
            ResourceState requiredState = isSrv ? ResourceState::ShaderResource : ResourceState::UnorderedAccess;
            uint32_t resourceIndex = rangeOffset.resource + baseIndex;
            for (uint32_t i = 0; i < count; ++i)
            {
                const ResourceSlot& slot = shaderObject->m_slots[slotIndex + i];
                BufferImpl* buffer = checked_cast<BufferImpl*>(slot.resource.get());
                BufferImpl* counterBuffer = checked_cast<BufferImpl*>(slot.resource2.get());
                if (bindingRangeInfo.isRootParameter)
                {
                    SLANG_RHI_ASSERT(rootParamIndex < m_bindingData->rootParameterCount);
                    if (buffer)
                    {
                        m_bindingData->rootParameters[rootParamIndex] =
                            isSrv ? createRootSRV(rootParamIndex, buffer->getDeviceAddress() + slot.bufferRange.offset)
                                  : createRootUAV(rootParamIndex, buffer->getDeviceAddress() + slot.bufferRange.offset);
                    }
                    rootParamIndex++;
                }
                else
                {
                    D3D12_CPU_DESCRIPTOR_HANDLE descriptor = {};
                    if (buffer)
                    {
                        if (isTyped)
                        {
                            descriptor =
                                isSrv ? buffer->getSRV(buffer->m_desc.format, 0, slot.bufferRange)
                                      : buffer->getUAV(buffer->m_desc.format, 0, slot.bufferRange, counterBuffer);
                        }
                        else
                        {
                            descriptor = isSrv ? buffer->getSRV(
                                                     Format::Undefined,
                                                     bindingRangeInfo.bufferElementStride,
                                                     slot.bufferRange
                                                 )
                                               : buffer->getUAV(
                                                     Format::Undefined,
                                                     bindingRangeInfo.bufferElementStride,
                                                     slot.bufferRange,
                                                     counterBuffer
                                                 );
                        }
                    }
                    else
                    {
                        descriptor =
                            m_device->getNullDescriptor(bindingRangeInfo.bindingType, bindingRangeInfo.resourceShape);
                    }
                    d3dDevice->CopyDescriptorsSimple(
                        1,
                        descriptorSet.resources.getCpuHandle(resourceIndex + i),
                        descriptor,
                        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
                    );
                }
                if (buffer)
                {
                    writeBufferState(this, buffer, requiredState);
                }
            }
            break;
        }
        case slang::BindingType::RayTracingAccelerationStructure:
        {
            uint32_t resourceIndex = rangeOffset.resource + baseIndex;
            for (uint32_t i = 0; i < count; ++i)
            {
                const ResourceSlot& slot = shaderObject->m_slots[slotIndex + i];
                AccelerationStructureImpl* as = checked_cast<AccelerationStructureImpl*>(slot.resource.get());
                if (bindingRangeInfo.isRootParameter)
                {
                    SLANG_RHI_ASSERT(rootParamIndex < m_bindingData->rootParameterCount);
                    if (as)
                    {
                        m_bindingData->rootParameters[rootParamIndex] =
                            createRootSRV(rootParamIndex, as->getDeviceAddress());
                    }
                    rootParamIndex++;
                }
                else
                {
                    SLANG_RHI_ASSERT(resourceIndex + i < descriptorSet.resources.count);
                    D3D12_CPU_DESCRIPTOR_HANDLE descriptor = {};
                    if (as)
                    {
                        descriptor = as->m_descriptor.cpuHandle;
                    }
                    else
                    {
                        descriptor =
                            m_device->getNullDescriptor(bindingRangeInfo.bindingType, bindingRangeInfo.resourceShape);
                    }
                    d3dDevice->CopyDescriptorsSimple(
                        1,
                        descriptorSet.resources.getCpuHandle(resourceIndex + i),
                        descriptor,
                        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
                    );
                }
                if (as)
                {
                    writeBufferState(this, as->m_buffer, ResourceState::AccelerationStructureRead);
                }
            }
            break;
        }
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
            for (uint32_t i = 0; i < count; ++i)
            {
                // Binding a constant buffer sub-object is simple enough:
                // we just call `bindAsConstantBuffer` on it to bind
                // the ordinary data buffer (if needed) and any other
                // bindings it recursively contains.
                //
                ShaderObject* subObject = shaderObject->m_objects[subObjectIndex + i];
                SLANG_RETURN_ON_FAIL(
                    bindAsConstantBuffer(subObject, descriptorSet, objOffset, rootParamIndex, subObjectLayout)
                );

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
                SLANG_RETURN_ON_FAIL(bindAsParameterBlock(subObject, objOffset, rootParamIndex, subObjectLayout));
                objOffset += rangeStride;
            }
        }
        break;

        case slang::BindingType::ExistentialValue:
            if (subObjectLayout)
            {
                auto objOffset = rangeOffset;
                for (uint32_t j = 0; j < bindingRangeInfo.count; j++)
                {
                    ShaderObject* subObject = shaderObject->m_objects[subObjectIndex + j];
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
                    if (subObjectLayout->getOrdinaryDataBufferCount() > 0)
                        objOffset.resource -= 1;
                    SLANG_RETURN_ON_FAIL(
                        bindAsValue(subObject, descriptorSet, objOffset, rootParamIndex, subObjectLayout)
                    );
                    objOffset += rangeStride;
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

Result BindingDataBuilder::bindOrdinaryDataBufferIfNeeded(
    ShaderObject* shaderObject,
    const DescriptorSet& descriptorSet,
    BindingOffset& ioOffset,
    ShaderObjectLayoutImpl* specializedLayout
)
{
    uint32_t size = specializedLayout->getTotalOrdinaryDataSize();
    if (size == 0)
    {
        return SLANG_OK;
    }

    // Constant buffer views need to be multiple of 256 bytes.
    uint32_t alignedSize = math::calcAligned2(size, 256);

    ConstantBufferPool::Allocation allocation;
    SLANG_RETURN_ON_FAIL(m_constantBufferPool->allocate(alignedSize, allocation));
    SLANG_RETURN_ON_FAIL(shaderObject->writeOrdinaryData(allocation.mappedData, size, specializedLayout));

    // We also create and store a descriptor for our root constant buffer
    // into the descriptor table allocation that was reserved for them.
    //
    // We always know that the ordinary data buffer will be the first descriptor
    // in the table of resource views.
    //
    // SLANG_RHI_ASSERT(ioOffset.resource == 0);
    D3D12_CONSTANT_BUFFER_VIEW_DESC viewDesc = {};
    viewDesc.BufferLocation = allocation.buffer->getDeviceAddress() + allocation.offset;
    viewDesc.SizeInBytes = alignedSize;
    m_device->m_device->CreateConstantBufferView(&viewDesc, descriptorSet.resources.getCpuHandle(ioOffset.resource));
    ioOffset.resource++;

    return SLANG_OK;
}

} // namespace rhi::d3d12
