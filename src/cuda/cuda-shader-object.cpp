#include "cuda-shader-object.h"
#include "cuda-helper-functions.h"
#include "cuda-shader-object-layout.h"
#include "cuda-device.h"
#include "cuda-acceleration-structure.h"

namespace rhi::cuda {

void shaderObjectSetBinding(
    ShaderObject* shaderObject,
    const ShaderOffset& offset,
    const ResourceSlot& slot,
    slang::BindingType bindingType
)
{
    uint8_t* dst = shaderObject->m_data.data();

    switch (bindingType)
    {
    case slang::BindingType::RawBuffer:
    case slang::BindingType::TypedBuffer:
    case slang::BindingType::MutableRawBuffer:
    case slang::BindingType::MutableTypedBuffer:
    {
        BufferImpl* buffer = checked_cast<BufferImpl*>(slot.resource.get());
        void* dataPtr = (uint8_t*)buffer->m_cudaMemory + slot.bufferRange.offset;
        size_t dataSize = slot.bufferRange.size;
        if (buffer->m_desc.elementSize > 1)
            dataSize /= buffer->m_desc.elementSize;
        memcpy(dst + offset.uniformOffset, &dataPtr, sizeof(dataPtr));
        memcpy(dst + offset.uniformOffset + 8, &dataSize, sizeof(dataSize));
        break;
    }
    case slang::BindingType::Texture:
    {
        TextureViewImpl* textureView = checked_cast<TextureViewImpl*>(slot.resource.get());
        uint64_t handle = textureView->m_texture->m_cudaTexObj;
        memcpy(dst + offset.uniformOffset, &handle, sizeof(handle));
        break;
    }
    case slang::BindingType::MutableTexture:
    {
        TextureViewImpl* textureView = checked_cast<TextureViewImpl*>(slot.resource.get());
        uint64_t handle = textureView->m_texture->m_cudaSurfObj;
        memcpy(dst + offset.uniformOffset, &handle, sizeof(handle));
        break;
    }
#if SLANG_RHI_ENABLE_OPTIX
    case slang::BindingType::RayTracingAccelerationStructure:
    {
        AccelerationStructureImpl* as = checked_cast<AccelerationStructureImpl*>(slot.resource.get());
        OptixTraversableHandle handle = as->m_handle;
        memcpy(dst + offset.uniformOffset, &handle, sizeof(handle));
        break;
    }
#endif
    default:
        break;
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
    m_bindingData = m_allocator->allocate<BindingDataImpl>();

    // Write global parameters
    {
        ObjectData data;
        SLANG_RETURN_ON_FAIL(writeObjectData(shaderObject, specializedLayout, data));
        m_bindingData->globalParams = data.device;
        m_bindingData->globalParamsSize = data.size;
    }

    // Write entry point parameters
    m_bindingData->entryPointCount = shaderObject->m_entryPoints.size();
    m_bindingData->entryPoints = m_allocator->allocate<BindingDataImpl::EntryPointData>(m_bindingData->entryPointCount);

    for (size_t i = 0; i < shaderObject->m_entryPoints.size(); ++i)
    {
        ShaderObject* entryPoint = shaderObject->m_entryPoints[i];
        const auto& entryPointInfo = specializedLayout->getEntryPoint(i);
        ShaderObjectLayoutImpl* entryPointLayout = entryPointInfo.layout;

        BindingDataImpl::EntryPointData& entryPointData = m_bindingData->entryPoints[i];
        ObjectData data;
        SLANG_RETURN_ON_FAIL(writeObjectData(entryPoint, entryPointLayout, data));
        entryPointData.data = data.host;
        entryPointData.size = data.size;
    }

    outBindingData = m_bindingData;

    return SLANG_OK;
}

Result BindingDataBuilder::writeObjectData(
    ShaderObject* shaderObject,
    ShaderObjectLayoutImpl* specializedLayout,
    ObjectData& outData
)
{
    size_t size = specializedLayout->getElementTypeLayout()->getSize();

    ConstantBufferPool::Allocation allocation;
    SLANG_RETURN_ON_FAIL(m_constantBufferPool->allocate(size, allocation));

    ObjectData objectData = {};
    objectData.size = size;
    objectData.host = allocation.hostData;
    objectData.device = allocation.deviceData;
    uint8_t* dst = (uint8_t*)objectData.host;

    shaderObject->writeOrdinaryData(dst, objectData.size, specializedLayout);

    // Bindings are currently written in shaderObjectSetBinding() because
    // the layout does currently only provide uniformOffset but no uniformStride.
#if 0
    for (const auto& bindingRange : specializedLayout->m_bindingRanges)
    {
        uint32_t count = bindingRange.count;
        uint32_t slotIndex = bindingRange.slotIndex;
        uint32_t uniformOffset = bindingRange.uniformOffset;
        uint32_t uniformStride = 0; // TODO we need this from the layout

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
                uint64_t handle = textureView->m_texture->m_cudaTexObj;
                memcpy(dst + uniformOffset + (i * uniformStride), &handle, sizeof(handle));
            }
            break;
        case slang::BindingType::MutableTexture:
            for (uint32_t i = 0; i < count; ++i)
            {
                const ResourceSlot& slot = shaderObject->m_slots[slotIndex + i];
                TextureViewImpl* textureView = checked_cast<TextureViewImpl*>(slot.resource.get());
                uint64_t handle = textureView->m_texture->m_cudaSurfObj;
                memcpy(dst + uniformOffset + (i * uniformStride), &handle, sizeof(handle));
            }
            break;
        case slang::BindingType::RawBuffer:
        case slang::BindingType::TypedBuffer:
        case slang::BindingType::MutableRawBuffer:
        case slang::BindingType::MutableTypedBuffer:
            for (uint32_t i = 0; i < count; ++i)
            {
                const ResourceSlot& slot = shaderObject->m_slots[slotIndex + i];
                BufferImpl* buffer = checked_cast<BufferImpl*>(slot.resource.get());
                void* dataPtr = (uint8_t*)buffer->m_cudaMemory + slot.bufferRange.offset;
                size_t dataSize = slot.bufferRange.size;
                if (buffer->m_desc.elementSize > 1)
                    dataSize /= buffer->m_desc.elementSize;
                memcpy(dst + uniformOffset + (i * uniformStride), &dataPtr, sizeof(dataPtr));
                memcpy(dst + uniformOffset + (i * uniformStride) + 8, &dataSize, sizeof(dataSize));
            }
            break;
#if SLANG_RHI_ENABLE_OPTIX
        case slang::BindingType::RayTracingAccelerationStructure:
            for (uint32_t i = 0; i < count; ++i)
            {
                const ResourceSlot& slot = shaderObject->m_slots[slotIndex + i];
                AccelerationStructureImpl* as = checked_cast<AccelerationStructureImpl*>(slot.resource.get());
                OptixTraversableHandle handle = as->m_handle;
                memcpy(dst + uniformOffset + (i * uniformStride), &handle, sizeof(handle));
            }
            break;
#endif
        }
    }
#endif

    // Once all the simple binding ranges are dealt with, we will bind
    // all of the sub-objects in sub-object ranges.
    //
    for (const auto& subObjectRange : specializedLayout->m_subObjectRanges)
    {
        ShaderObjectLayoutImpl* subObjectLayout = subObjectRange.layout;
        const auto& bindingRange = specializedLayout->m_bindingRanges[subObjectRange.bindingRangeIndex];
        uint32_t count = bindingRange.count;
        uint32_t subObjectIndex = bindingRange.subObjectIndex;
        size_t uniformOffset = bindingRange.uniformOffset;

        switch (bindingRange.bindingType)
        {
        case slang::BindingType::ConstantBuffer:
        case slang::BindingType::ParameterBlock:
        {
            for (uint32_t i = 0; i < count; ++i)
            {
                ShaderObject* subObject = shaderObject->m_objects[subObjectIndex + i];

                ObjectData data;
                SLANG_RETURN_ON_FAIL(writeObjectData(subObject, subObjectLayout, data));
                ::memcpy(dst + uniformOffset, &data.device, sizeof(void*));
                uniformOffset += sizeof(void*);
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
                // The data for objects in this range will always be bound into
                // the "pending" allocation for the parent block/buffer/object.
                // As a result, the offset for the first object in the range
                // will come from the `pending` part of the range's offset.
                //
                SimpleBindingOffset objOffset = rangeOffset.pending;
                SimpleBindingOffset objStride = rangeStride.pending;

                for (uint32_t i = 0; i < count; ++i)
                {
                    ShaderObject* subObject = shaderObject->m_objects[subObjectIndex + i];
                    bindAsValue(subObject, BindingOffset(objOffset), subObjectLayout);

                    objOffset += objStride;
                }
            }
            break;
#endif
        default:
            break;
        }
    }

    outData = objectData;

    return SLANG_OK;
}

void BindingCache::reset() {}

} // namespace rhi::cuda
