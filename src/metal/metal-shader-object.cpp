#include "metal-shader-object.h"
#include "metal-device.h"
#include "metal-buffer.h"
#include "metal-texture.h"
#include "metal-sampler.h"
#include <slang.h>

namespace rhi::metal {

inline Result setBuffer(BindingDataImpl* bindingData, uint32_t index, MTL::Buffer* buffer, NS::UInteger offset = 0)
{
    if (index >= bindingData->bufferCapacity)
    {
        return SLANG_FAIL;
    }
    bindingData->bufferCount = max(bindingData->bufferCount, index + 1);
    bindingData->buffers[index] = buffer;
    bindingData->bufferOffsets[index] = offset;
    return SLANG_OK;
}

inline Result setTexture(BindingDataImpl* bindingData, uint32_t index, MTL::Texture* texture)
{
    if (index >= bindingData->textureCapacity)
    {
        return SLANG_FAIL;
    }
    bindingData->textureCount = max(bindingData->textureCount, index + 1);
    bindingData->textures[index] = texture;
    return SLANG_OK;
}

inline Result addUsedResource(BindingDataImpl* bindingData, MTL::Resource* resource)
{
    if (bindingData->usedResourceCount >= bindingData->usedResourceCapacity)
    {
        return SLANG_FAIL;
    }
    bindingData->usedResources[bindingData->usedResourceCount++] = resource;
    return SLANG_OK;
}

inline Result addUsedRWResource(BindingDataImpl* bindingData, MTL::Resource* resource)
{
    if (bindingData->usedRWResourceCount >= bindingData->usedRWResourceCapacity)
    {
        return SLANG_FAIL;
    }
    bindingData->usedRWResources[bindingData->usedRWResourceCount++] = resource;
    return SLANG_OK;
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

    // TODO(shaderobject): we should count number of buffers/textures in the layout and allocate appropriately
    // then we could switch to asserts instead of error checks when writing binding data
    m_bindingData->bufferCapacity = 256;
    m_bindingData->textureCapacity = 256;
    m_bindingData->usedResourceCapacity = 256;
    m_bindingData->usedRWResourceCapacity = 256;

    m_bindingData->bufferCount = 0;
    m_bindingData->buffers = m_allocator->allocate<MTL::Buffer*>(m_bindingData->bufferCapacity);
    ::memset(m_bindingData->buffers, 0, sizeof(MTL::Buffer*) * m_bindingData->bufferCapacity);
    m_bindingData->bufferOffsets = m_allocator->allocate<NS::UInteger>(m_bindingData->bufferCapacity);
    ::memset(m_bindingData->bufferOffsets, 0, sizeof(NS::UInteger) * m_bindingData->bufferCapacity);

    m_bindingData->textureCount = 0;
    m_bindingData->textures = m_allocator->allocate<MTL::Texture*>(m_bindingData->textureCapacity);
    ::memset(m_bindingData->textures, 0, sizeof(MTL::Texture*) * m_bindingData->textureCapacity);

    uint32_t samplerCount = specializedLayout->getTotalSamplerCount();
    m_bindingData->samplerCount = samplerCount;
    m_bindingData->samplers = m_allocator->allocate<MTL::SamplerState*>(samplerCount);
    ::memset(m_bindingData->samplers, 0, sizeof(MTL::SamplerState*) * samplerCount);

    m_bindingData->usedResourceCount = 0;
    m_bindingData->usedResources = m_allocator->allocate<MTL::Resource*>(m_bindingData->usedResourceCapacity);
    m_bindingData->usedRWResourceCount = 0;
    m_bindingData->usedRWResources = m_allocator->allocate<MTL::Resource*>(m_bindingData->usedRWResourceCapacity);

    // Initialize binding offset for shader parameters.
    //
    BindingOffset offset;

    // Note: We could *almost* call `bindAsConstantBuffer()` here to bind
    // the state of the root object itself, but there is an important
    // detail that means we can't:
    //
    // The `_bindOrdinaryDataBufferIfNeeded` operation automatically
    // increments the offset parameter if it binds a buffer, so that
    // subsequently bindings will be adjusted. However, the reflection
    // information computed for root shader parameters is absolute rather
    // than relative to the default constant buffer (if any).
    //
    // TODO: Quite technically, the ordinary data buffer for the global
    // scope is *not* guaranteed to be at offset zero, so this logic should
    // really be querying an appropriate absolute offset from `layout`.
    //
#if 1
    BindingOffset ordinaryDataBufferOffset = offset;
    SLANG_RETURN_ON_FAIL(bindOrdinaryDataBufferIfNeeded(shaderObject, ordinaryDataBufferOffset, specializedLayout));
#endif
    SLANG_RETURN_ON_FAIL(bindAsValue(shaderObject, offset, specializedLayout));

    // Once the state stored in the root shader object itself has been bound,
    // we turn our attention to the entry points and their parameters.
    //
    size_t entryPointCount = specializedLayout->m_entryPoints.size();
    for (size_t i = 0; i < entryPointCount; ++i)
    {
        auto entryPoint = shaderObject->m_entryPoints[i];
        const auto& entryPointInfo = specializedLayout->m_entryPoints[i];
        ShaderObjectLayoutImpl* entryPointLayout = entryPointInfo.layout;

        // Each entry point will be bound at some offset relative to where
        // the root shader parameters start.
        //
        BindingOffset entryPointOffset = offset;
        entryPointOffset += entryPointInfo.offset;

        // An entry point can simply be bound as a constant buffer, because
        // the absolute offsets as are used for the global scope do not apply
        // (because entry points don't need to deal with explicit bindings).
        //
        SLANG_RETURN_ON_FAIL(bindAsConstantBuffer(entryPoint, entryPointOffset, entryPointLayout));
    }

    outBindingData = bindingData;

    return SLANG_OK;
}

/// Bind this object as if it was declared as a `ConstantBuffer<T>` in Slang
Result BindingDataBuilder::bindAsConstantBuffer(
    ShaderObject* shaderObject,
    const BindingOffset& inOffset,
    ShaderObjectLayoutImpl* specializedLayout
)
{
    // When binding a `ConstantBuffer<X>` we need to first bind a constant
    // buffer for any "ordinary" data in `X`, and then bind the remaining
    // resources and sub-objects.
    //
    BindingOffset offset = inOffset;
    SLANG_RETURN_ON_FAIL(bindOrdinaryDataBufferIfNeeded(shaderObject, /*inout*/ offset, specializedLayout));

    // Once the ordinary data buffer is bound, we can move on to binding
    // the rest of the state, which can use logic shared with the case
    // for interface-type sub-object ranges.
    //
    // Note that this call will use the `inOffset` value instead of the offset
    // modified by `_bindOrindaryDataBufferIfNeeded', because the indexOffset in
    // the binding range should already take care of the offset due to the default
    // cbuffer.
    //
    SLANG_RETURN_ON_FAIL(bindAsValue(shaderObject, inOffset, specializedLayout));

    return SLANG_OK;
}

/// Bind this object as if it was declared as a `ParameterBlock<T>` in Slang
Result BindingDataBuilder::bindAsParameterBlock(
    ShaderObject* shaderObject,
    const BindingOffset& inOffset,
    ShaderObjectLayoutImpl* specializedLayout
)
{
    if (!m_device->m_hasArgumentBufferTier2)
        return SLANG_FAIL;

    BufferImpl* argumentBuffer = nullptr;
    SLANG_RETURN_ON_FAIL(writeArgumentBuffer(shaderObject, specializedLayout, argumentBuffer));

    if (argumentBuffer)
    {
        SLANG_RETURN_ON_FAIL(setBuffer(m_bindingData, inOffset.buffer, argumentBuffer->m_buffer.get()));
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
            for (uint32_t i = 0; i < count; ++i)
            {
                const ResourceSlot& slot = shaderObject->m_slots[slotIndex + i];
                TextureViewImpl* textureView = checked_cast<TextureViewImpl*>(slot.resource.get());
                if (textureView)
                {
                    uint32_t registerIndex = bindingRangeInfo.registerOffset + offset.texture + i;
                    SLANG_RETURN_ON_FAIL(setTexture(m_bindingData, registerIndex, textureView->m_textureView.get()));
                }
            }
            break;
        case slang::BindingType::Sampler:
            for (uint32_t i = 0; i < count; ++i)
            {
                const ResourceSlot& slot = shaderObject->m_slots[slotIndex + i];
                SamplerImpl* sampler = checked_cast<SamplerImpl*>(slot.resource.get());
                if (sampler)
                {
                    uint32_t registerIndex = bindingRangeInfo.registerOffset + offset.sampler + i;
                    SLANG_RHI_ASSERT(registerIndex < m_bindingData->samplerCount);
                    m_bindingData->samplers[registerIndex] = sampler->m_samplerState.get();
                }
            }
            break;
        case slang::BindingType::RawBuffer:
        case slang::BindingType::MutableRawBuffer:
        case slang::BindingType::TypedBuffer:
        case slang::BindingType::MutableTypedBuffer:
            for (uint32_t i = 0; i < count; ++i)
            {
                const ResourceSlot& slot = shaderObject->m_slots[slotIndex + i];
                BufferImpl* buffer = checked_cast<BufferImpl*>(slot.resource.get());
                if (buffer)
                {
                    uint32_t registerIndex = bindingRangeInfo.registerOffset + offset.buffer + i;
                    SLANG_RETURN_ON_FAIL(
                        setBuffer(m_bindingData, registerIndex, buffer->m_buffer.get(), slot.bufferRange.offset)
                    );
                }
            }
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

    // Once all the simple binding ranges are dealt with, we will bind
    // all of the sub-objects in sub-object ranges.
    //
    for (const auto& subObjectRange : specializedLayout->m_subObjectRanges)
    {
        auto subObjectLayout = subObjectRange.layout;
        const auto& bindingRange = specializedLayout->m_bindingRanges[subObjectRange.bindingRangeIndex];
        uint32_t count = bindingRange.count;
        uint32_t subObjectIndex = bindingRange.subObjectIndex;

        // The starting offset for a sub-object range was computed
        // from Slang reflection information, so we can apply it here.
        //
        BindingOffset rangeOffset = offset;
        rangeOffset += subObjectRange.offset;

        // Similarly, the "stride" between consecutive objects in
        // the range was also pre-computed.
        //
        BindingOffset rangeStride = subObjectRange.stride;

        switch (bindingRange.bindingType)
        {
        case slang::BindingType::ConstantBuffer:
        {
            BindingOffset objOffset = rangeOffset;
            for (uint32_t i = 0; i < count; ++i)
            {
                auto subObject = shaderObject->m_objects[subObjectIndex + i];

                // Unsurprisingly, we bind each object in the range as
                // a constant buffer.
                //
                SLANG_RETURN_ON_FAIL(bindAsConstantBuffer(subObject, objOffset, subObjectLayout));

                objOffset += rangeStride;
            }
            break;
        }
        case slang::BindingType::ParameterBlock:
        {
            BindingOffset objOffset = rangeOffset;
            for (uint32_t i = 0; i < count; ++i)
            {
                auto subObject = shaderObject->m_objects[subObjectIndex + i];
                SLANG_RETURN_ON_FAIL(bindAsParameterBlock(subObject, objOffset, subObjectLayout));
                objOffset += rangeStride;
            }
        }
        break;

#if 0
        case slang::BindingType::ExistentialValue:
            // We can only bind information for existential-typed sub-object
            // ranges if we have a static type that we are able to specialize to.
            //
            if (subObjectLayout)
            {
                SimpleBindingOffset objOffset = {};
                SimpleBindingOffset objStride = {};

                for (Index i = 0; i < count; ++i)
                {
                    auto subObject = m_objects[subObjectIndex + i];
                    subObject->bindAsValue(context, BindingOffset(objOffset), subObjectLayout);

                    objOffset += objStride;
                }
            }
            break;
#endif

        default:
            break;
        }
    }

    return SLANG_OK;
}

Result BindingDataBuilder::bindOrdinaryDataBufferIfNeeded(
    ShaderObject* shaderObject,
    BindingOffset& ioOffset,
    ShaderObjectLayoutImpl* specializedLayout
)
{
    uint32_t size = specializedLayout->getTotalOrdinaryDataSize();
    if (size == 0)
        return SLANG_OK;

    ComPtr<IBuffer> buffer;
    BufferDesc bufferDesc = {};
    bufferDesc.size = size;
    bufferDesc.usage = BufferUsage::ConstantBuffer | BufferUsage::CopyDestination;
    bufferDesc.defaultState = ResourceState::ConstantBuffer;
    bufferDesc.memoryType = MemoryType::Upload;
    SLANG_RETURN_ON_FAIL(m_device->createBuffer(bufferDesc, nullptr, buffer.writeRef()));
    auto bufferImpl = checked_cast<BufferImpl*>(buffer.get());

    // Once the buffer is allocated, we can use `_writeOrdinaryData` to fill it in.
    //
    // Note that `_writeOrdinaryData` is potentially recursive in the case
    // where this object contains interface/existential-type fields, so we
    // don't need or want to inline it into this call site.
    //
    void* ordinaryData = bufferImpl->m_buffer->contents();
    SLANG_RETURN_ON_FAIL(shaderObject->writeOrdinaryData(ordinaryData, size, specializedLayout));

    // If we did indeed need/create a buffer, then we must bind it
    // into root binding state.
    //
    SLANG_RETURN_ON_FAIL(setBuffer(m_bindingData, ioOffset.buffer, bufferImpl->m_buffer.get()));
    ioOffset.buffer++;

    bufferImpl->m_buffer->didModifyRange(NS::Range(0, bufferImpl->m_desc.size));

    // Pass ownership of the buffer to the binding cache.
    m_bindingCache->buffers.push_back(bufferImpl);

    return SLANG_OK;
}

Result BindingDataBuilder::writeArgumentBuffer(
    ShaderObject* shaderObject,
    ShaderObjectLayoutImpl* specializedLayout,
    BufferImpl*& outArgumentBuffer
)
{
    auto argumentBufferTypeLayout = specializedLayout->getParameterBlockTypeLayout();

    // If the argument buffer has no fields, we don't need to create one, note this is legal because there could be an
    // empty struct type in AST. We need to handle this correctly.
    if (argumentBufferTypeLayout->getFieldCount() == 0)
    {
        outArgumentBuffer = nullptr;
        return SLANG_OK;
    }

    ComPtr<IBuffer> argumentBuffer;
    BufferDesc argumentBufferDesc = {};
    argumentBufferDesc.size = argumentBufferTypeLayout->getSize();
    argumentBufferDesc.usage = BufferUsage::ConstantBuffer | BufferUsage::CopyDestination;
    argumentBufferDesc.defaultState = ResourceState::ConstantBuffer;
    argumentBufferDesc.memoryType = MemoryType::Upload;
    SLANG_RETURN_ON_FAIL(m_device->createBuffer(argumentBufferDesc, nullptr, argumentBuffer.writeRef()));
    auto argumentBufferImpl = checked_cast<BufferImpl*>(argumentBuffer.get());

    memcpy(argumentBufferImpl->m_buffer->contents(), shaderObject->m_data.data(), shaderObject->m_data.size());

    // Once the buffer is allocated, we can fill it in with the uniform data
    // and resource bindings we have tracked, using `argumentBufferTypeLayout` to obtain
    // the offsets for each field.
    //
    uint8_t* argumentData = (uint8_t*)argumentBufferImpl->m_buffer->contents();

    // Write all ordinary data early to prevent overwriting gpu-addresses
    // we write below.
    SLANG_RETURN_ON_FAIL(writeOrdinaryDataIntoArgumentBuffer(
        argumentBufferTypeLayout,
        shaderObject->getElementTypeLayout(),
        (uint8_t*)argumentData,
        (uint8_t*)shaderObject->m_data.data()
    ));

    for (uint32_t bindingRangeIndex = 0; bindingRangeIndex < specializedLayout->getBindingRangeCount();
         ++bindingRangeIndex)
    {
        const auto& bindingRangeInfo = specializedLayout->m_bindingRanges[bindingRangeIndex];
        uint32_t slotIndex = bindingRangeInfo.slotIndex;
        uint32_t count = bindingRangeInfo.count;

        SlangInt setIndex = argumentBufferTypeLayout->getBindingRangeDescriptorSetIndex(bindingRangeIndex);
        SlangInt rangeIndex = argumentBufferTypeLayout->getBindingRangeFirstDescriptorRangeIndex(bindingRangeIndex);
        SlangInt argumentOffset =
            argumentBufferTypeLayout->getDescriptorSetDescriptorRangeIndexOffset(setIndex, rangeIndex);
        uint8_t* argumentPtr = argumentData + argumentOffset;

        switch (bindingRangeInfo.bindingType)
        {
        case slang::BindingType::ConstantBuffer:
        case slang::BindingType::ParameterBlock:
        case slang::BindingType::ExistentialValue:
            break;

        case slang::BindingType::Texture:
        case slang::BindingType::MutableTexture:
            for (uint32_t i = 0; i < count; ++i)
            {
                const ResourceSlot& slot = shaderObject->m_slots[slotIndex + i];
                TextureViewImpl* textureView = checked_cast<TextureViewImpl*>(slot.resource.get());
                if (textureView)
                {
                    auto resourceId = textureView->m_textureView->gpuResourceID();
                    memcpy(argumentPtr + i * sizeof(uint64_t), &resourceId, sizeof(resourceId));
                    if (bindingRangeInfo.bindingType == slang::BindingType::MutableTexture)
                    {
                        SLANG_RETURN_ON_FAIL(addUsedRWResource(m_bindingData, textureView->m_textureView.get()));
                    }
                    else
                    {
                        SLANG_RETURN_ON_FAIL(addUsedResource(m_bindingData, textureView->m_textureView.get()));
                    }
                }
            }
            break;
        case slang::BindingType::Sampler:
            for (uint32_t i = 0; i < count; ++i)
            {
                const ResourceSlot& slot = shaderObject->m_slots[slotIndex + i];
                SamplerImpl* sampler = checked_cast<SamplerImpl*>(slot.resource.get());
                if (sampler)
                {
                    auto resourceId = sampler->m_samplerState->gpuResourceID();
                    memcpy(argumentPtr + i * sizeof(uint64_t), &resourceId, sizeof(resourceId));
                }
            }
            break;
        case slang::BindingType::RawBuffer:
        case slang::BindingType::MutableRawBuffer:
        case slang::BindingType::TypedBuffer:
        case slang::BindingType::MutableTypedBuffer:
        {
            for (uint32_t i = 0; i < count; ++i)
            {
                const ResourceSlot& slot = shaderObject->m_slots[slotIndex + i];
                BufferImpl* buffer = checked_cast<BufferImpl*>(slot.resource.get());
                if (buffer)
                {
                    DeviceAddress bufferPtr = buffer->getDeviceAddress() + slot.bufferRange.offset;
                    memcpy(argumentPtr + i * sizeof(uint64_t), &bufferPtr, sizeof(bufferPtr));
                    if (bindingRangeInfo.bindingType == slang::BindingType::MutableRawBuffer ||
                        bindingRangeInfo.bindingType == slang::BindingType::MutableTypedBuffer)
                    {
                        SLANG_RETURN_ON_FAIL(addUsedRWResource(m_bindingData, buffer->m_buffer.get()));
                    }
                    else
                    {
                        SLANG_RETURN_ON_FAIL(addUsedResource(m_bindingData, buffer->m_buffer.get()));
                    }
                }
            }
        }
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

    for (const auto& subObjectRange : specializedLayout->m_subObjectRanges)
    {
        auto subObjectLayout = subObjectRange.layout;
        const auto& bindingRange = specializedLayout->m_bindingRanges[subObjectRange.bindingRangeIndex];
        uint32_t count = bindingRange.count;
        uint32_t subObjectIndex = bindingRange.subObjectIndex;

        switch (bindingRange.bindingType)
        {
        case slang::BindingType::ParameterBlock:
        case slang::BindingType::ConstantBuffer:
        {
            SlangInt setIndex =
                argumentBufferTypeLayout->getBindingRangeDescriptorSetIndex(subObjectRange.bindingRangeIndex);
            SlangInt rangeIndex =
                argumentBufferTypeLayout->getBindingRangeFirstDescriptorRangeIndex(subObjectRange.bindingRangeIndex);
            SlangInt argumentOffset =
                argumentBufferTypeLayout->getDescriptorSetDescriptorRangeIndexOffset(setIndex, rangeIndex);
            uint8_t* argumentPtr = argumentData + argumentOffset;

            for (uint32_t i = 0; i < count; ++i)
            {
                auto subObject = shaderObject->m_objects[subObjectIndex + i];
                BufferImpl* subArgumentBuffer = nullptr;
                SLANG_RETURN_ON_FAIL(writeArgumentBuffer(subObject, subObjectLayout, subArgumentBuffer));
                DeviceAddress bufferPtr = subArgumentBuffer->m_buffer->gpuAddress();
                memcpy(argumentPtr + i * sizeof(uint64_t), &bufferPtr, sizeof(bufferPtr));
                SLANG_RETURN_ON_FAIL(addUsedResource(m_bindingData, subArgumentBuffer->m_buffer.get()));
            }
            break;
        }
        default:
            break;
        }
    }

    argumentBufferImpl->m_buffer->didModifyRange(NS::Range(0, argumentBufferImpl->m_desc.size));

    // Pass ownership of the buffer to the binding cache.
    m_bindingCache->buffers.push_back(argumentBufferImpl);

    outArgumentBuffer = argumentBufferImpl;
    return SLANG_OK;
}

Result BindingDataBuilder::writeOrdinaryDataIntoArgumentBuffer(
    slang::TypeLayoutReflection* argumentBufferTypeLayout,
    slang::TypeLayoutReflection* defaultTypeLayout,
    uint8_t* argumentBuffer,
    uint8_t* srcData
)
{
    // If we are pure data, just copy it over from srcData.
    if (defaultTypeLayout->getCategoryCount() == 1)
    {
        if (defaultTypeLayout->getCategoryByIndex(0) == slang::ParameterCategory::Uniform)
        {
            // Just write the uniform data.
            memcpy(argumentBuffer, srcData, defaultTypeLayout->getSize());
        }
        return SLANG_OK;
    }

    for (unsigned int i = 0; i < argumentBufferTypeLayout->getFieldCount(); i++)
    {
        auto argumentBufferField = argumentBufferTypeLayout->getFieldByIndex(i);
        auto defaultLayoutField = defaultTypeLayout->getFieldByIndex(i);
        // If the field is mixed type, recurse.
        SLANG_RETURN_ON_FAIL(writeOrdinaryDataIntoArgumentBuffer(
            argumentBufferField->getTypeLayout(),
            defaultLayoutField->getTypeLayout(),
            argumentBuffer + argumentBufferField->getOffset(),
            srcData + defaultLayoutField->getOffset()
        ));
    }
    return SLANG_OK;
}

} // namespace rhi::metal
