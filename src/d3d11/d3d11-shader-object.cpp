#include "d3d11-shader-object.h"
#include "d3d11-device.h"
#include "d3d11-buffer.h"
#include "d3d11-texture.h"
#include "d3d11-sampler.h"

namespace rhi::d3d11 {

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
    ::memset(bindingData, 0, sizeof(BindingDataImpl));

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
    // really be querying an appropriate absolute offset from `specializedLayout`.
    //
    BindingOffset ordinaryDataBufferOffset = offset;
    SLANG_RETURN_ON_FAIL(
        bindOrdinaryDataBufferIfNeeded(shaderObject, /*inout*/ ordinaryDataBufferOffset, specializedLayout)
    );
    SLANG_RETURN_ON_FAIL(bindAsValue(shaderObject, offset, specializedLayout));

    // Once the state stored in the root shader object itself has been bound,
    // we turn our attention to the entry points and their parameters.
    //
    for (size_t i = 0; i < shaderObject->m_entryPoints.size(); ++i)
    {
        ShaderObject* entryPoint = shaderObject->m_entryPoints[i];
        const auto& entryPointInfo = specializedLayout->getEntryPoint(i);
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

Result BindingDataBuilder::bindAsValue(
    ShaderObject* shaderObject,
    const BindingOffset& offset,
    ShaderObjectLayoutImpl* specializedLayout
)
{
    // We start by iterating over the binding ranges in this type, isolating
    // just those ranges that represent SRVs, UAVs, and samplers.
    // In each loop we will bind the values stored for those binding ranges
    // to the correct D3D11 register (based on the `registerOffset` field
    // stored in the bindinge range).
    //
    // TODO: These loops could be optimized if we stored parallel arrays
    // for things like `m_srvs` so that we directly store an array of
    // `ID3D11ShaderResourceView*` where each entry matches the `rhi`-level
    // object that was bound (or holds null if nothing is bound).
    // In that case, we could perform a single `setSRVs()` call for each
    // binding range.
    //
    // TODO: More ambitiously, if the Slang layout algorithm could be modified
    // so that non-sub-object binding ranges are guaranteed to be contiguous
    // then a *single* `setSRVs()` call could set all of the SRVs for an object
    // at once.

    for (const auto& bindingRange : specializedLayout->m_bindingRanges)
    {
        uint32_t count = bindingRange.count;
        uint32_t slotIndex = bindingRange.slotIndex;

        switch (bindingRange.bindingType)
        {
        case slang::BindingType::ConstantBuffer:
        case slang::BindingType::ParameterBlock:
        case slang::BindingType::ExistentialValue:
            break;
        case slang::BindingType::Texture:
            for (uint32_t i = 0; i < count; ++i)
            {
                const ResourceSlot& slot = shaderObject->m_slots[slotIndex + i];
                TextureViewImpl* textureView = checked_cast<TextureViewImpl*>(slot.resource.get());
                if (textureView)
                {
                    uint32_t registerIndex = bindingRange.registerOffset + offset.srv + i;
                    SLANG_RHI_ASSERT(registerIndex < D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT);
                    m_bindingData->srvs[registerIndex] = textureView->getSRV();
                    m_bindingData->srvCount = max(m_bindingData->srvCount, registerIndex + 1);
                }
            }
            break;
        case slang::BindingType::MutableTexture:
            for (uint32_t i = 0; i < count; ++i)
            {
                const ResourceSlot& slot = shaderObject->m_slots[slotIndex + i];
                TextureViewImpl* textureView = checked_cast<TextureViewImpl*>(slot.resource.get());
                if (textureView)
                {
                    uint32_t registerIndex = bindingRange.registerOffset + offset.uav + i;
                    SLANG_RHI_ASSERT(registerIndex < D3D11_PS_CS_UAV_REGISTER_COUNT);
                    m_bindingData->uavs[registerIndex] = textureView->getUAV();
                    m_bindingData->uavCount = max(m_bindingData->uavCount, registerIndex + 1);
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
                    uint32_t registerIndex = bindingRange.registerOffset + offset.sampler + i;
                    SLANG_RHI_ASSERT(registerIndex < D3D11_COMMONSHADER_SAMPLER_REGISTER_COUNT);
                    m_bindingData->samplers[registerIndex] = sampler->m_sampler;
                    m_bindingData->samplerCount = max(m_bindingData->samplerCount, registerIndex + 1);
                }
            }
            break;
        case slang::BindingType::RawBuffer:
        case slang::BindingType::TypedBuffer:
            for (uint32_t i = 0; i < count; ++i)
            {
                const ResourceSlot& slot = shaderObject->m_slots[slotIndex + i];
                BufferImpl* buffer = checked_cast<BufferImpl*>(slot.resource.get());
                if (buffer)
                {
                    uint32_t registerIndex = bindingRange.registerOffset + offset.srv + i;
                    SLANG_RHI_ASSERT(registerIndex < D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT);
                    m_bindingData->srvs[registerIndex] = buffer->getSRV(slot.format, slot.bufferRange);
                    m_bindingData->srvCount = max(m_bindingData->srvCount, registerIndex + 1);
                }
            }
            break;
        case slang::BindingType::MutableRawBuffer:
        case slang::BindingType::MutableTypedBuffer:
            for (uint32_t i = 0; i < count; ++i)
            {
                const ResourceSlot& slot = shaderObject->m_slots[slotIndex + i];
                BufferImpl* buffer = checked_cast<BufferImpl*>(slot.resource.get());
                if (buffer)
                {
                    uint32_t registerIndex = bindingRange.registerOffset + offset.uav + i;
                    SLANG_RHI_ASSERT(registerIndex < D3D11_PS_CS_UAV_REGISTER_COUNT);
                    m_bindingData->uavs[registerIndex] = buffer->getUAV(slot.format, slot.bufferRange);
                    m_bindingData->uavCount = max(m_bindingData->uavCount, registerIndex + 1);
                }
            }
            break;
        default:
            break;
        }
    }

    // Once all the simple binding ranges are dealt with, we will bind
    // all of the sub-objects in sub-object ranges.
    //
    for (const auto& subObjectRange : specializedLayout->m_subObjectRanges)
    {
        ShaderObjectLayoutImpl* subObjectLayout = subObjectRange.layout;
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
            // For D3D11-compatible compilation targets, the Slang compiler
            // treats the `ConstantBuffer<T>` and `ParameterBlock<T>` types the same.
            //
        case slang::BindingType::ConstantBuffer:
        case slang::BindingType::ParameterBlock:
        {
            BindingOffset objOffset = rangeOffset;
            for (uint32_t i = 0; i < count; ++i)
            {
                ShaderObject* subObject = shaderObject->m_objects[subObjectIndex + i];

                // Unsurprisingly, we bind each object in the range as
                // a constant buffer.
                //
                SLANG_RETURN_ON_FAIL(bindAsConstantBuffer(subObject, objOffset, subObjectLayout));

                objOffset += rangeStride;
            }
        }
        break;

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
    auto size = specializedLayout->m_totalOrdinaryDataSize;
    if (size == 0)
        return SLANG_OK;

    ConstantBufferPool::Allocation allocation;
    SLANG_RETURN_ON_FAIL(m_constantBufferPool->allocate(size, allocation));
    SLANG_RETURN_ON_FAIL(shaderObject->writeOrdinaryData(allocation.mappedData, size, specializedLayout));

    SLANG_RHI_ASSERT(ioOffset.cbv < D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT);
    m_bindingData->cbvsBuffer[ioOffset.cbv] = allocation.buffer->m_buffer;
    m_bindingData->cbvsFirst[ioOffset.cbv] = allocation.offset / 16;
    m_bindingData->cbvsCount[ioOffset.cbv] = ((size + 15) / 16) * 16;
    m_bindingData->cbvCount = max(m_bindingData->cbvCount, ioOffset.cbv + 1);
    ioOffset.cbv++;

    return SLANG_OK;
}

void BindingCache::reset() {}

} // namespace rhi::d3d11
