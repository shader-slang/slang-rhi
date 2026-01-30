#include "cuda-shader-object.h"
#include "cuda-utils.h"
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
    SLANG_CUDA_CTX_SCOPE(checked_cast<DeviceImpl*>(shaderObject->m_device.get()));

    uint8_t* dst = shaderObject->m_data.data();

    switch (bindingType)
    {
    case slang::BindingType::RawBuffer:
    case slang::BindingType::TypedBuffer:
    case slang::BindingType::MutableRawBuffer:
    case slang::BindingType::MutableTypedBuffer:
    {
        BufferImpl* buffer = checked_cast<BufferImpl*>(slot.resource.get());
        void* dataPtr = nullptr;
        size_t dataSize = 0;
        if (buffer)
        {
            dataPtr = (uint8_t*)buffer->m_cudaMemory + slot.bufferRange.offset;
            dataSize = slot.bufferRange.size;
            if (buffer->m_desc.elementSize > 1)
                dataSize /= buffer->m_desc.elementSize;
        }
        memcpy(dst + offset.uniformOffset, &dataPtr, sizeof(dataPtr));
        memcpy(dst + offset.uniformOffset + 8, &dataSize, sizeof(dataSize));
        break;
    }
    case slang::BindingType::Texture:
    {
        TextureViewImpl* textureView = checked_cast<TextureViewImpl*>(slot.resource.get());
        uint64_t handle = 0;
        if (textureView)
        {
            handle = textureView->getTexObject();
        }
        memcpy(dst + offset.uniformOffset, &handle, sizeof(handle));
        break;
    }
    case slang::BindingType::MutableTexture:
    {
        TextureViewImpl* textureView = checked_cast<TextureViewImpl*>(slot.resource.get());
        uint64_t handle = 0;
        if (textureView)
        {
            handle = textureView->getSurfObject();
        }
        memcpy(dst + offset.uniformOffset, &handle, sizeof(handle));
        break;
    }
    case slang::BindingType::CombinedTextureSampler:
    {
        TextureViewImpl* textureView = checked_cast<TextureViewImpl*>(slot.resource.get());
        SamplerImpl* sampler = checked_cast<SamplerImpl*>(slot.resource2.get());
        uint64_t handle = 0;
        if (textureView && sampler)
        {
            handle = textureView->getTexObjectWithSamplerSettings(sampler->m_samplerSettings);
        }
        memcpy(dst + offset.uniformOffset, &handle, sizeof(handle));
        break;
    }
    case slang::BindingType::RayTracingAccelerationStructure:
    {
        AccelerationStructureImpl* as = checked_cast<AccelerationStructureImpl*>(slot.resource.get());
        uint64_t handle = 0;
        if (as)
        {
            handle = as->m_handle;
        }
        memcpy(dst + offset.uniformOffset, &handle, sizeof(handle));
        break;
    }
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
        SLANG_RETURN_ON_FAIL(writeObjectData(shaderObject, specializedLayout, ConstantBufferMemType::Global, data));
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
        SLANG_RETURN_ON_FAIL(writeObjectData(entryPoint, entryPointLayout, ConstantBufferMemType::EntryPoint, data));
        entryPointData.data = data.host;
        entryPointData.size = data.size;
        // Adjust the entry point parameter buffer size to match what is expected by cuLaunchKernel.
        SLANG_RHI_ASSERT(entryPointInfo.paramsSize <= entryPointData.size);
        entryPointData.size = entryPointInfo.paramsSize;
    }

    outBindingData = m_bindingData;

    return SLANG_OK;
}

Result BindingDataBuilder::writeObjectData(
    ShaderObject* shaderObject,
    ShaderObjectLayoutImpl* specializedLayout,
    ConstantBufferMemType memType,
    ObjectData& outData
)
{
    size_t size = specializedLayout->getElementTypeLayout()->getSize();

    ConstantBufferPool::Allocation allocation;
    SLANG_RETURN_ON_FAIL(m_constantBufferPool->allocate(size, memType, allocation));

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
                uint64_t handle = textureView->getTexObject();
                memcpy(dst + uniformOffset + (i * uniformStride), &handle, sizeof(handle));
            }
            break;
        case slang::BindingType::MutableTexture:
            for (uint32_t i = 0; i < count; ++i)
            {
                const ResourceSlot& slot = shaderObject->m_slots[slotIndex + i];
                TextureViewImpl* textureView = checked_cast<TextureViewImpl*>(slot.resource.get());
                uint64_t handle = textureView->getSurfObject();;
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
        case slang::BindingType::RayTracingAccelerationStructure:
            for (uint32_t i = 0; i < count; ++i)
            {
                const ResourceSlot& slot = shaderObject->m_slots[slotIndex + i];
                AccelerationStructureImpl* as = checked_cast<AccelerationStructureImpl*>(slot.resource.get());
                OptixTraversableHandle handle = as->m_handle;
                memcpy(dst + uniformOffset + (i * uniformStride), &handle, sizeof(handle));
            }
            break;
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

                // Sub-objects are always written to global memory, even if the parent represents an entry-point.
                // This is because entry-point data reference global memory for their sub-objects (parameter blocks).
                ObjectData data;
                SLANG_RETURN_ON_FAIL(writeObjectData(subObject, subObjectLayout, ConstantBufferMemType::Global, data));
                ::memcpy(dst + uniformOffset, &data.device, sizeof(void*));
                uniformOffset += sizeof(void*);
            }
        }
        break;
        default:
            break;
        }
    }

    outData = objectData;

    return SLANG_OK;
}

void BindingCache::reset() {}

} // namespace rhi::cuda
