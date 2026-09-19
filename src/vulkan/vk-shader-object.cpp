#include "vk-shader-object.h"
#include "vk-device.h"
#include "vk-buffer.h"
#include "vk-texture.h"
#include "vk-sampler.h"
#include "vk-acceleration-structure.h"
#include "vk-shader-object-layout.h"
#include "vk-bindless-descriptor-set.h"
#include "vk-command.h"

#include "../state-tracking.h"

#include <string>

namespace rhi::vk {

PreparedBindingData::~PreparedBindingData()
{
    if (device)
    {
        std::lock_guard<std::mutex> lock(device->m_persistentDescriptorMutex);
        for (auto set : descriptorSets)
            device->descriptorSetAllocator.free(set);
    }
}

BindingDataStorage::BindingDataStorage(CommandBufferImpl& commandBuffer)
    : rhi::BindingDataStorage(
          commandBuffer.m_allocator,
          commandBuffer.m_trackedObjects,
          commandBuffer.m_constantBufferArena
      )
    , m_device(commandBuffer.getDevice<DeviceImpl>())
    , m_bindingCache(commandBuffer.m_bindingCache)
    , m_descriptorSetAllocator(commandBuffer.m_descriptorSetAllocator)
{
}

BindingDataStorage::BindingDataStorage(PreparedBindingData& prepared)
    : rhi::BindingDataStorage(prepared)
    , m_device(prepared.device)
    , m_bindingCache(prepared.bindingCache)
    , m_descriptorSetAllocator(prepared.device->descriptorSetAllocator)
    , m_persistentDescriptorSets(&prepared.descriptorSets)
{
}

BindingDataImpl* BindingDataStorage::allocateBindingData()
{
    auto data = allocate<BindingDataImpl>();
    m_bindingCache.bindingData.push_back(data);
    return data;
}

Result BindingDataStorage::allocateDescriptorSet(VkDescriptorSetLayout layout, VkDescriptorSet& outSet)
{
    outSet = VK_NULL_HANDLE;
    if (m_persistentDescriptorSets)
    {
        std::lock_guard<std::mutex> lock(m_device->m_persistentDescriptorMutex);
        auto allocation = m_descriptorSetAllocator.allocate(layout);
        if (!allocation.handle)
            return SLANG_E_OUT_OF_MEMORY;
        m_persistentDescriptorSets->push_back(allocation);
        outSet = allocation.handle;
    }
    else
    {
        outSet = m_descriptorSetAllocator.allocate(layout).handle;
    }
    return outSet ? SLANG_OK : SLANG_E_OUT_OF_MEMORY;
}

inline void writeDescriptor(DeviceImpl* device, const VkWriteDescriptorSet& write)
{
    device->m_api.vkUpdateDescriptorSets(device->m_device, 1, &write, 0, nullptr);
}

inline void writePlainBufferDescriptor(
    DeviceImpl* device,
    VkDescriptorSet descriptorSet,
    uint32_t binding,
    uint32_t index,
    VkDescriptorType descriptorType,
    BufferImpl* buffer,
    BufferRange range
)
{
    VkDescriptorBufferInfo bufferInfo = {};
    bufferInfo.range = VK_WHOLE_SIZE;

    if (buffer)
    {
        bufferInfo.buffer = buffer->m_buffer.m_buffer;
        bufferInfo.offset = range.offset;
        bufferInfo.range = range.size;
    }

    VkWriteDescriptorSet write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = descriptorSet;
    write.dstBinding = binding;
    write.dstArrayElement = index;
    write.descriptorCount = 1;
    write.descriptorType = descriptorType;
    write.pBufferInfo = &bufferInfo;

    writeDescriptor(device, write);
}

inline void writeTexelBufferDescriptor(
    DeviceImpl* device,
    VkDescriptorSet descriptorSet,
    uint32_t binding,
    uint32_t index,
    VkDescriptorType descriptorType,
    BufferImpl* buffer,
    Format format,
    BufferRange range
)
{
    VkBufferView bufferView = buffer ? buffer->getView(format, range) : VK_NULL_HANDLE;

    VkWriteDescriptorSet write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = descriptorSet;
    write.dstBinding = binding;
    write.dstArrayElement = index;
    write.descriptorCount = 1;
    write.descriptorType = descriptorType;
    write.pTexelBufferView = &bufferView;
    writeDescriptor(device, write);
}

inline void writeTextureSamplerDescriptor(
    DeviceImpl* device,
    VkDescriptorSet descriptorSet,
    uint32_t binding,
    uint32_t index,
    TextureViewImpl* textureView,
    SamplerImpl* sampler
)
{
    VkDescriptorImageInfo imageInfo = {};
    if (textureView && sampler)
    {
        imageInfo.imageView = textureView->getView().imageView;
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.sampler = sampler->m_sampler;
    }

    VkWriteDescriptorSet write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = descriptorSet;
    write.dstBinding = binding;
    write.dstArrayElement = index;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imageInfo;

    writeDescriptor(device, write);
}

inline void writeAccelerationStructureDescriptor(
    DeviceImpl* device,
    VkDescriptorSet descriptorSet,
    uint32_t binding,
    uint32_t index,
    AccelerationStructureImpl* as
)
{
    // The Vulkan spec states: If the nullDescriptor feature is not enabled, each element of
    // pAccelerationStructures must not be VK_NULL_HANDLE
    if (!as && !device->m_api.m_extendedFeatures.robustness2Features.nullDescriptor)
    {
        SLANG_RHI_ASSERT_FAILURE("nullDescriptor feature is not available on the device");
        return;
    }

    VkWriteDescriptorSetAccelerationStructureKHR writeAS = {};
    writeAS.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
    writeAS.accelerationStructureCount = 1;
    static const VkAccelerationStructureKHR nullHandle = VK_NULL_HANDLE;
    writeAS.pAccelerationStructures = as ? &as->m_vkHandle : &nullHandle;

    VkWriteDescriptorSet write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = descriptorSet;
    write.dstBinding = binding;
    write.dstArrayElement = index;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    write.pNext = &writeAS;
    writeDescriptor(device, write);
}

inline void writeTextureDescriptor(
    DeviceImpl* device,
    VkDescriptorSet descriptorSet,
    uint32_t binding,
    uint32_t index,
    VkDescriptorType descriptorType,
    TextureViewImpl* textureView
)
{
    VkDescriptorImageInfo imageInfo = {};
    if (textureView)
    {
        imageInfo.imageView = textureView->getView().imageView;
        imageInfo.imageLayout = descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
                                    ? VK_IMAGE_LAYOUT_GENERAL
                                    : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    imageInfo.sampler = 0;

    VkWriteDescriptorSet write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = descriptorSet;
    write.dstBinding = binding;
    write.dstArrayElement = index;
    write.descriptorCount = 1;
    write.descriptorType = descriptorType;
    write.pImageInfo = &imageInfo;

    writeDescriptor(device, write);
}

inline void writeSamplerDescriptor(
    DeviceImpl* device,
    VkDescriptorSet descriptorSet,
    uint32_t binding,
    uint32_t index,
    SamplerImpl* sampler
)
{
    VkDescriptorImageInfo imageInfo = {};
    imageInfo.imageView = 0;
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    imageInfo.sampler = sampler ? sampler->m_sampler : device->m_defaultSampler;

    VkWriteDescriptorSet write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = descriptorSet;
    write.dstBinding = binding;
    write.dstArrayElement = index;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    write.pImageInfo = &imageInfo;

    writeDescriptor(device, write);
}

inline void writeBufferState(BindingDataBuilder* builder, BufferImpl* buffer, ResourceState state)
{
    BindingDataImpl* bindingData = builder->m_bindingData;
    if (bindingData->bufferStateCount >= bindingData->bufferStateCapacity)
    {
        bindingData->bufferStateCapacity *= 2;
        BindingDataImpl::BufferState* newBufferStates =
            builder->m_storage.allocate<BindingDataImpl::BufferState>(bindingData->bufferStateCapacity);
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
            builder->m_storage.allocate<BindingDataImpl::TextureState>(bindingData->textureStateCapacity);
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
    if (shaderObject->isFinalized())
    {
        PreparedBindingData* data;
        SLANG_RETURN_ON_FAIL(shaderObject->getPreparedData<PreparedBindingData>(
            specializedLayout,
            {},
            [&](PreparedBindingData* data)
            {
                data->init(m_device);
                BindingDataStorage storage(*data);
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
    m_bindingData = m_storage.allocateBindingData();

    // TODO(shaderobject): we should count number of buffers/textures in the layout and allocate appropriately
    // For now we use a fixed starting capacity and grow as needed.
    m_bindingData->bufferStateCapacity = 1024;
    m_bindingData->bufferStates = m_storage.allocate<BindingDataImpl::BufferState>(m_bindingData->bufferStateCapacity);
    m_bindingData->bufferStateCount = 0;
    m_bindingData->textureStateCapacity = 1024;
    m_bindingData->textureStates =
        m_storage.allocate<BindingDataImpl::TextureState>(m_bindingData->textureStateCapacity);
    m_bindingData->textureStateCount = 0;

    m_bindingData->pipelineLayout = specializedLayout->m_pipelineLayout;

    uint32_t totalDescriptorSetCount = specializedLayout->getTotalDescriptorSetCount();
    if (m_device->m_bindlessDescriptorSet)
    {
        // The bindless descriptor set is always the last descriptor set in the pipeline layout.
        // We need to add one more descriptor set to the count to account for it.
        totalDescriptorSetCount++;
    }
    m_bindingData->descriptorSets = m_storage.allocate<VkDescriptorSet>(totalDescriptorSetCount);
    m_bindingData->descriptorSetCount = 0;

    // Allocate entry point data storage for ray tracing SBT.
    size_t entryPointCount = specializedLayout->m_entryPoints.size();
    m_bindingData->entryPointCount = (uint32_t)entryPointCount;
    if (specializedLayout->findEntryPointIndex(VK_SHADER_STAGE_RAYGEN_BIT_KHR) != -1)
    {
        m_bindingData->entryPointData = m_storage.allocate<BindingDataImpl::EntryPointData>(entryPointCount);
        for (size_t i = 0; i < entryPointCount; ++i)
        {
            m_bindingData->entryPointData[i].data = nullptr;
            m_bindingData->entryPointData[i].size = 0;
        }
    }
    else
    {
        m_bindingData->entryPointData = nullptr;
    }

    BindingOffset offset = {};

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

    for (size_t i = 0; i < entryPointCount; ++i)
    {
        auto entryPoint = shaderObject->m_entryPoints[i];
        const auto& entryPointInfo = specializedLayout->m_entryPoints[i];
        EntryPointLayout* entryPointLayout = entryPointInfo.layout;

        // Note: we do *not* need to add the entry point offset
        // information to the global `offset` because the
        // `RootShaderObjectLayout` has already baked any offsets
        // from the global layout into the `entryPointInfo`.

        SLANG_RETURN_ON_FAIL(bindAsEntryPoint(entryPoint, entryPointInfo.offset, entryPointLayout, (uint32_t)i));
    }

    // Assign bindless descriptor set to the last slot if available.
    if (m_device->m_bindlessDescriptorSet)
    {
        m_bindingData->descriptorSets[m_bindingData->descriptorSetCount++] =
            m_device->m_bindlessDescriptorSet->m_descriptorSet;
    }

    SLANG_RETURN_ON_FAIL(resolvePushConstants(specializedLayout->getAllPushConstantRanges()));
    outBindingData = m_bindingData;

    return SLANG_OK;
}

Result BindingDataBuilder::bindAsEntryPoint(
    ShaderObject* shaderObject,
    const BindingOffset& inOffset,
    EntryPointLayout* layout,
    uint32_t entryPointIndex
)
{
    if (layout->getSlangLayout()->getStage() != SLANG_STAGE_RAY_GENERATION)
    {
        // For non-raygen entry points, ordinary data goes into push constants.
        return bindAsPushConstantBuffer(shaderObject, inOffset, layout);
    }

    // For raygen entry points, ordinary data is stored in the SBT instead.
    if (shaderObject->m_data.size())
    {
        SLANG_RHI_ASSERT(m_bindingData->entryPointData && entryPointIndex < m_bindingData->entryPointCount);
        BindingDataImpl::EntryPointData& epData = m_bindingData->entryPointData[entryPointIndex];
        epData.size = shaderObject->m_data.size();
        epData.data = m_storage.allocate(epData.size);
        ::memcpy(epData.data, shaderObject->m_data.data(), epData.size);
    }

    SLANG_RETURN_ON_FAIL(bindAsValue(shaderObject, inOffset, layout));

    return SLANG_OK;
}

Result BindingDataBuilder::bindAsPushConstantBuffer(
    ShaderObject* shaderObject,
    const BindingOffset& inOffset,
    ShaderObjectLayoutImpl* specializedLayout
)
{
    BindingOffset offset = inOffset;

    if (shaderObject->m_data.size())
    {
        PushConstantBinding binding;
        binding.rangeIndex = offset.pushConstantRange++;
        binding.size = uint32_t(shaderObject->m_data.size());
        binding.data = m_storage.allocate(binding.size);
        SLANG_RETURN_ON_FAIL(shaderObject->writeOrdinaryData(binding.data, binding.size, specializedLayout));
        m_pushConstants.push_back(binding);
    }

    // Resources and nested parameter blocks in the push-constant element type still
    // need their normal recursive binding treatment.
    SLANG_RETURN_ON_FAIL(bindAsValue(shaderObject, offset, specializedLayout));

    return SLANG_OK;
}


Result BindingDataBuilder::resolvePushConstants(std::span<const VkPushConstantRange> ranges)
{
    // Only the assembled root knows the pipeline's absolute offsets and stage masks.
    m_bindingData->pushConstantCount = uint32_t(m_pushConstants.size());
    m_bindingData->pushConstantRanges = m_storage.allocate<VkPushConstantRange>(m_pushConstants.size());
    m_bindingData->pushConstantData = m_storage.allocate<void*>(m_pushConstants.size());
    for (uint32_t i = 0; i < m_pushConstants.size(); ++i)
    {
        const auto& binding = m_pushConstants[i];
        if (binding.rangeIndex >= ranges.size() || ranges[binding.rangeIndex].size != binding.size)
            return SLANG_E_INVALID_ARG;
        m_bindingData->pushConstantRanges[i] = ranges[binding.rangeIndex];
        m_bindingData->pushConstantData[i] = binding.data;
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
    {
        return SLANG_OK;
    }

    BindingDataStorage::UniformData allocation;
    SLANG_RETURN_ON_FAIL(m_storage.writeOrdinaryData(shaderObject, specializedLayout, size, size, allocation));

    // If we did indeed need/create a buffer, then we must bind it into
    // the given `descriptorSet` and update the base range index for
    // subsequent binding operations to account for it.
    //
    writePlainBufferDescriptor(
        m_device,
        m_bindingData->descriptorSets[ioOffset.bindingSet],
        ioOffset.binding,
        0,
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        checked_cast<BufferImpl*>(allocation.buffer),
        {allocation.offset, size}
    );
    ioOffset.binding++;

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
    for (auto bindingRangeInfo : specializedLayout->getBindingRanges())
    {
        BindingOffset rangeOffset = offset;
        rangeOffset.bindingSet += bindingRangeInfo.setOffset;
        rangeOffset.binding += bindingRangeInfo.bindingOffset;

        DeviceImpl* device = m_device;
        uint32_t binding = rangeOffset.binding;

        uint32_t slotIndex = bindingRangeInfo.slotIndex;
        uint32_t count = bindingRangeInfo.count;

        switch (bindingRangeInfo.bindingType)
        {
        case slang::BindingType::ConstantBuffer:
        case slang::BindingType::ParameterBlock:
        case slang::BindingType::ExistentialValue:
        case slang::BindingType::PushConstant:
            break;

        case slang::BindingType::Texture:
        case slang::BindingType::MutableTexture:
        {
            VkDescriptorSet descriptorSet = m_bindingData->descriptorSets[rangeOffset.bindingSet];
            VkDescriptorType descriptorType = bindingRangeInfo.bindingType == slang::BindingType::Texture
                                                  ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
                                                  : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            ResourceState requiredState = bindingRangeInfo.bindingType == slang::BindingType::Texture
                                              ? ResourceState::ShaderResource
                                              : ResourceState::UnorderedAccess;
            for (uint32_t i = 0; i < count; ++i)
            {
                const ResourceSlot& slot = shaderObject->m_slots[slotIndex + i];
                TextureViewImpl* textureView = checked_cast<TextureViewImpl*>(slot.resource.get());
                writeTextureDescriptor(device, descriptorSet, binding, i, descriptorType, textureView);
                if (textureView)
                {
                    writeTextureState(this, textureView, requiredState);
                }
            }
            break;
        }
        case slang::BindingType::CombinedTextureSampler:
        {
            VkDescriptorSet descriptorSet = m_bindingData->descriptorSets[rangeOffset.bindingSet];
            ResourceState requiredState = ResourceState::ShaderResource;
            for (uint32_t i = 0; i < count; ++i)
            {
                const ResourceSlot& slot = shaderObject->m_slots[slotIndex + i];
                TextureViewImpl* textureView = checked_cast<TextureViewImpl*>(slot.resource.get());
                SamplerImpl* sampler = checked_cast<SamplerImpl*>(slot.resource2.get());
                writeTextureSamplerDescriptor(device, descriptorSet, binding, i, textureView, sampler);
                if (textureView)
                {
                    writeTextureState(this, textureView, requiredState);
                }
            }
            break;
        }
        case slang::BindingType::Sampler:
        {
            VkDescriptorSet descriptorSet = m_bindingData->descriptorSets[rangeOffset.bindingSet];
            for (uint32_t i = 0; i < count; ++i)
            {
                const ResourceSlot& slot = shaderObject->m_slots[slotIndex + i];
                SamplerImpl* sampler = checked_cast<SamplerImpl*>(slot.resource.get());
                writeSamplerDescriptor(device, descriptorSet, binding, i, sampler);
            }
            break;
        }
        case slang::BindingType::RawBuffer:
        case slang::BindingType::MutableRawBuffer:
        {
            VkDescriptorSet descriptorSet = m_bindingData->descriptorSets[rangeOffset.bindingSet];
            // TODO: should RawBuffer map to VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER?
            VkDescriptorType descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            ResourceState requiredState = bindingRangeInfo.bindingType == slang::BindingType::RawBuffer
                                              ? ResourceState::ShaderResource
                                              : ResourceState::UnorderedAccess;
            for (uint32_t i = 0; i < count; ++i)
            {
                const ResourceSlot& slot = shaderObject->m_slots[slotIndex + i];
                BufferImpl* buffer = checked_cast<BufferImpl*>(slot.resource.get());
                writePlainBufferDescriptor(device, descriptorSet, binding, i, descriptorType, buffer, slot.bufferRange);
                if (buffer)
                {
                    writeBufferState(this, buffer, requiredState);
                }
            }
            break;
        }
        case slang::BindingType::TypedBuffer:
        case slang::BindingType::MutableTypedBuffer:
        {
            VkDescriptorSet descriptorSet = m_bindingData->descriptorSets[rangeOffset.bindingSet];
            VkDescriptorType descriptorType = bindingRangeInfo.bindingType == slang::BindingType::TypedBuffer
                                                  ? VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER
                                                  : VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
            ResourceState requiredState = bindingRangeInfo.bindingType == slang::BindingType::TypedBuffer
                                              ? ResourceState::ShaderResource
                                              : ResourceState::UnorderedAccess;
            for (uint32_t i = 0; i < count; ++i)
            {
                const ResourceSlot& slot = shaderObject->m_slots[slotIndex + i];
                BufferImpl* buffer = checked_cast<BufferImpl*>(slot.resource.get());
                writeTexelBufferDescriptor(
                    device,
                    descriptorSet,
                    binding,
                    i,
                    descriptorType,
                    buffer,
                    slot.format,
                    slot.bufferRange
                );
                if (buffer)
                {
                    writeBufferState(this, buffer, requiredState);
                }
            }
            break;
        }
        case slang::BindingType::RayTracingAccelerationStructure:
        {
            VkDescriptorSet descriptorSet = m_bindingData->descriptorSets[rangeOffset.bindingSet];
            for (uint32_t i = 0; i < count; ++i)
            {
                const ResourceSlot& slot = shaderObject->m_slots[slotIndex + i];
                AccelerationStructureImpl* as = checked_cast<AccelerationStructureImpl*>(slot.resource.get());
                writeAccelerationStructureDescriptor(device, descriptorSet, binding, i, as);
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
        {
            std::string message = "Unsupported binding type: " + std::to_string((int)bindingRangeInfo.bindingType);
            SLANG_RHI_ASSERT_FAILURE(message.c_str());
            return SLANG_FAIL;
        }
        }
    }

    // Once we've handled the simple binding ranges, we move on to the
    // sub-object ranges, which are generally more involved.
    //
    for (const auto& subObjectRange : specializedLayout->getSubObjectRanges())
    {
        const auto& bindingRangeInfo = specializedLayout->getBindingRange(subObjectRange.bindingRangeIndex);
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

        case slang::BindingType::PushConstant:
        {
            BindingOffset objOffset = rangeOffset;
            for (uint32_t i = 0; i < count; ++i)
            {
                ShaderObject* subObject = shaderObject->m_objects[subObjectIndex + i];
                SLANG_RETURN_ON_FAIL(bindAsPushConstantBuffer(subObject, objOffset, subObjectLayout));
                objOffset += rangeStride;
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
                // Interface-typed sub-object ranges are no longer supported
                // after pending data layout APIs have been removed.
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

Result BindingDataBuilder::allocateDescriptorSets(
    ShaderObject* shaderObject,
    const BindingOffset& offset,
    ShaderObjectLayoutImpl* specializedLayout
)
{
    SLANG_RHI_ASSERT(specializedLayout->getOwnDescriptorSets().size() <= 1);
    // The number of sets to allocate and their layouts was already pre-computed
    // as part of the shader object layout, so we use that information here.
    //
    for (auto descriptorSetInfo : specializedLayout->getOwnDescriptorSets())
    {
        VkDescriptorSet descriptorSetHandle;
        SLANG_RETURN_ON_FAIL(
            m_storage.allocateDescriptorSet(descriptorSetInfo.descriptorSetLayout, descriptorSetHandle)
        );

        // For each set, we need to write it into the set of descriptor sets
        // being used for binding. This is done both so that other steps
        // in binding can find the set to fill it in, but also so that
        // we can bind all the descriptor sets to the pipeline when the
        // time comes.
        //
        m_bindingData->descriptorSets[m_bindingData->descriptorSetCount++] = descriptorSetHandle;
    }

    return SLANG_OK;
}

Result BindingDataBuilder::bindAsParameterBlock(
    ShaderObject* shaderObject,
    const BindingOffset& inOffset,
    ShaderObjectLayoutImpl* specializedLayout
)
{
    if (!shaderObject->isFinalized())
        return bindAsParameterBlockImpl(shaderObject, inOffset, specializedLayout);
    const ParameterBlockBindingData* data;
    SLANG_RETURN_ON_FAIL(prepareParameterBlock(shaderObject, specializedLayout, data));
    composeParameterBlock(*data, inOffset);
    return SLANG_OK;
}

Result BindingDataBuilder::prepareParameterBlock(
    ShaderObject* shaderObject,
    ShaderObjectLayoutImpl* specializedLayout,
    const ParameterBlockBindingData*& outData
)
{
    struct PreparedParameterBlock : PreparedBindingData
    {
        ParameterBlockBindingData block;
    };
    PreparedParameterBlock* data;
    SLANG_RETURN_ON_FAIL(shaderObject->getPreparedData<PreparedParameterBlock>(
        specializedLayout,
        {},
        [&](PreparedParameterBlock* data)
        {
            data->init(m_device);
            BindingDataStorage storage(*data);
            BindingDataBuilder builder(storage);
            BindingDataImpl bindings = {};
            bindings.bufferStateCapacity = bindings.textureStateCapacity = 16;
            bindings.bufferStates = storage.allocate<BindingDataImpl::BufferState>(16);
            bindings.textureStates = storage.allocate<BindingDataImpl::TextureState>(16);
            bindings.descriptorSets =
                storage.allocate<VkDescriptorSet>(specializedLayout->getTotalDescriptorSetCount());
            builder.m_bindingData = &bindings;
            SLANG_RETURN_ON_FAIL(builder.bindAsParameterBlockImpl(shaderObject, BindingOffset{}, specializedLayout));
            auto pushConstants = storage.allocate<PushConstantBinding>(builder.m_pushConstants.size());
            std::copy(builder.m_pushConstants.begin(), builder.m_pushConstants.end(), pushConstants);
            data->block.descriptorSets = {bindings.descriptorSets, bindings.descriptorSetCount};
            data->block.pushConstants = {pushConstants, builder.m_pushConstants.size()};
            data->block.bufferStates = {bindings.bufferStates, bindings.bufferStateCount};
            data->block.textureStates = {bindings.textureStates, bindings.textureStateCount};
            return SLANG_OK;
        },
        data
    ));
    m_storage.retain(data);
    outData = &data->block;
    return SLANG_OK;
}

void BindingDataBuilder::composeParameterBlock(const ParameterBlockBindingData& data, const BindingOffset& offset)
{
    for (auto set : data.descriptorSets)
        m_bindingData->descriptorSets[m_bindingData->descriptorSetCount++] = set;
    for (auto binding : data.pushConstants)
    {
        binding.rangeIndex += offset.pushConstantRange;
        m_pushConstants.push_back(binding);
    }
    for (const auto& state : data.bufferStates)
        writeBufferState(this, state.buffer, state.state);
    for (const auto& state : data.textureStates)
        writeTextureState(this, state.textureView, state.state);
}

Result BindingDataBuilder::bindAsParameterBlockImpl(
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
    offset.bindingSet = m_bindingData->descriptorSetCount;
    offset.binding = 0;

    // Note: Interface-type binding handling has been simplified
    // now that pending data layout APIs have been removed.

    // Writing the bindings for a parameter block is relatively easy:
    // we just need to allocate the descriptor set(s) needed for this
    // object and then fill it in like a `ConstantBuffer<X>`.
    //
    SLANG_RETURN_ON_FAIL(allocateDescriptorSets(shaderObject, offset, specializedLayout));

    SLANG_RHI_ASSERT(offset.bindingSet < m_bindingData->descriptorSetCount);
    SLANG_RETURN_ON_FAIL(bindAsConstantBuffer(shaderObject, offset, specializedLayout));

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

} // namespace rhi::vk
