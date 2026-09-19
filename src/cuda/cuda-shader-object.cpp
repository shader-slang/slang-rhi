#include "cuda-shader-object.h"
#include "cuda-command.h"
#include "cuda-utils.h"
#include "cuda-shader-object-layout.h"
#include "cuda-device.h"
#include "cuda-acceleration-structure.h"

namespace rhi::cuda {

BindingDataStorage::BindingDataStorage(CommandBufferImpl& commandBuffer)
    : rhi::BindingDataStorage(commandBuffer.m_allocator, commandBuffer.m_trackedObjects)
    , m_device(commandBuffer.getDevice<DeviceImpl>())
    , m_constantBufferPool(&commandBuffer.m_constantBufferPool)
{
}

BindingDataStorage::BindingDataStorage(DeviceImpl* device, PreparedShaderObject& prepared)
    : rhi::BindingDataStorage(prepared)
    , m_device(device)
{
}

void BindingDataStorage::trackResources(ShaderObject* shaderObject)
{
    if (shaderObject->isFinalized())
    {
        rhi::BindingDataStorage::trackResources(shaderObject);
        return;
    }
    // Track slot resources, but skip device-local buffers
    for (const auto& slot : shaderObject->m_slots)
    {
        if (slot.resource)
        {
            // Check if this is a device-local buffer we can skip
            if (Buffer* buffer = dynamic_cast<Buffer*>(slot.resource.get()))
            {
                // Only skip DeviceLocal buffers - these benefit from same-stream reuse
                // Keep tracking Upload/ReadBack buffers as CPU may access them
                if (buffer->m_desc.memoryType == MemoryType::DeviceLocal)
                {
                    continue; // Skip tracking - CUDA stream ordering provides safety
                }
            }
            retain(slot.resource);
        }
        if (slot.resource2)
        {
            // resource2 is typically a sampler or counter buffer, always track
            retain(slot.resource2);
        }
    }

    // Recursively track sub-objects
    for (const auto& object : shaderObject->m_objects)
    {
        if (object)
        {
            trackResources(object.get());
        }
    }
}

void BindingDataStorage::trackResources(RootShaderObject* rootObject)
{
    trackResources(static_cast<ShaderObject*>(rootObject));
    for (const auto& entryPoint : rootObject->m_entryPoints)
    {
        if (entryPoint)
        {
            trackResources(entryPoint.get());
        }
    }
}

Result BindingDataStorage::allocateObjectData(
    ShaderObject* object,
    size_t size,
    ConstantBufferMemType memType,
    ConstantBufferPool::Allocation& allocation
)
{
    allocation = {};
    if (isPersistent())
    {
        if (!object->isFinalized())
            return SLANG_E_INVALID_ARG;
        allocation.hostData = size ? allocate(size) : nullptr;
        return SLANG_OK;
    }
    return m_constantBufferPool->allocate(size, memType, allocation);
}

Result BindingDataStorage::finishObjectData(
    size_t size,
    ConstantBufferMemType memType,
    ConstantBufferPool::Allocation& allocation
)
{
    if (isPersistent() && memType == ConstantBufferMemType::Global && size)
    {
        BufferDesc desc;
        desc.size = size;
        desc.usage = BufferUsage::ConstantBuffer;
        desc.defaultState = ResourceState::ConstantBuffer;
        desc.memoryType = MemoryType::DeviceLocal;
        ComPtr<IBuffer> buffer;
        // Initialization completes before the prepared graph's device pointer is published.
        SLANG_RETURN_ON_FAIL(m_device->createBuffer(desc, allocation.hostData, buffer.writeRef()));
        auto bufferImpl = checked_cast<BufferImpl*>(buffer.get());
        allocation.deviceData = bufferImpl->getDeviceAddress();
        retain(bufferImpl);
    }
    return SLANG_OK;
}

struct PreparedBindingData : PreparedShaderObject
{
    BindingDataImpl* bindingData = nullptr;
};

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
    if (shaderObject->isFinalized())
    {
        PreparedBindingData* data;
        SLANG_RETURN_ON_FAIL(shaderObject->getPreparedData<PreparedBindingData>(
            specializedLayout,
            {},
            [&](PreparedBindingData* data)
            {
                BindingDataStorage storage(m_device, *data);
                BindingDataBuilder builder(storage);
                return builder.bindAsRootImpl(shaderObject, specializedLayout, data->bindingData);
            },
            data
        ));
        m_storage.retain(data);
        outBindingData = data->bindingData;
        return SLANG_OK;
    }

    return bindAsRootImpl(shaderObject, specializedLayout, outBindingData);
}

Result BindingDataBuilder::bindAsRootImpl(
    RootShaderObject* shaderObject,
    RootShaderObjectLayoutImpl* specializedLayout,
    BindingDataImpl*& outBindingData
)
{
    // Create a new set of binding data to populate.
    m_bindingData = m_storage.allocate<BindingDataImpl>();

    // Write global parameters
    {
        ObjectData data;
        SLANG_RETURN_ON_FAIL(writeObjectData(shaderObject, specializedLayout, ConstantBufferMemType::Global, data));
        m_bindingData->globalParams = data.device;
        m_bindingData->globalParamsSize = data.size;
    }

    // Write entry point parameters
    m_bindingData->entryPointCount = shaderObject->m_entryPoints.size();
    m_bindingData->entryPoints = m_storage.allocate<BindingDataImpl::EntryPointData>(m_bindingData->entryPointCount);

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
    if (!shaderObject->isFinalized())
        return writeObjectDataImpl(shaderObject, specializedLayout, memType, outData);
    struct PreparedObjectData : PreparedShaderObject
    {
        ObjectData data = {};
    };
    PreparedObjectData* data;
    SLANG_RETURN_ON_FAIL(shaderObject->getPreparedData<PreparedObjectData>(
        specializedLayout,
        {uint64_t(memType)},
        [&](PreparedObjectData* data)
        {
            BindingDataStorage storage(m_device, *data);
            BindingDataBuilder builder(storage);
            return builder.writeObjectDataImpl(shaderObject, specializedLayout, memType, data->data);
        },
        data
    ));
    m_storage.retain(data);
    outData = data->data;
    return SLANG_OK;
}

Result BindingDataBuilder::writeObjectDataImpl(
    ShaderObject* shaderObject,
    ShaderObjectLayoutImpl* specializedLayout,
    ConstantBufferMemType memType,
    ObjectData& outData
)
{
    size_t size = specializedLayout->getElementTypeLayout()->getSize();

    ConstantBufferPool::Allocation allocation = {};
    SLANG_RETURN_ON_FAIL(m_storage.allocateObjectData(shaderObject, size, memType, allocation));

    ObjectData objectData = {};
    objectData.size = size;
    objectData.host = allocation.hostData;
    objectData.device = allocation.deviceData;
    uint8_t* dst = (uint8_t*)objectData.host;

    SLANG_RETURN_ON_FAIL(shaderObject->writeOrdinaryData(dst, objectData.size, specializedLayout));

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

    SLANG_RETURN_ON_FAIL(m_storage.finishObjectData(size, memType, allocation));
    objectData.device = allocation.deviceData;
    outData = objectData;

    return SLANG_OK;
}

void BindingCache::reset() {}

} // namespace rhi::cuda
