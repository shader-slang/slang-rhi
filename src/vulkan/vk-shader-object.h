#pragma once

#include "vk-base.h"
#include "vk-descriptor-heap.h"
#include "vk-shader-object-layout.h"
#include "../transient-buffer-heap.h"

#include "core/short_vector.h"

#include <vector>

namespace rhi::vk {

struct DescriptorSetBinding
{
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    const ShaderObjectLayoutImpl::DescriptorSetInfo* layout = nullptr;
    uint32_t resourceOffset = 0;
    uint32_t samplerOffset = 0;

    uint32_t slot(uint32_t binding, uint32_t index, bool sampler = false) const
    {
        uint32_t offset = sampler ? samplerOffset : resourceOffset;
        for (const auto& range : layout->vkBindings)
        {
            if (range.binding == binding)
            {
                SLANG_RHI_ASSERT(index < range.descriptorCount);
                return offset + index;
            }
            if (sampler ? (range.descriptorType == VK_DESCRIPTOR_TYPE_SAMPLER || range.descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                        : range.descriptorType != VK_DESCRIPTOR_TYPE_SAMPLER)
                offset += range.descriptorCount;
        }
        SLANG_RHI_ASSERT_FAILURE("Descriptor binding is absent from heap layout");
        return offset;
    }
};

struct BindingDataBuilder
{
    DeviceImpl* m_device;
    ArenaAllocator* m_allocator;
    BindingCache* m_bindingCache;
    BindingDataImpl* m_bindingData;
    TransientBufferArena* m_constantBufferArena;
    DescriptorSetAllocator* m_descriptorSetAllocator;

    // TODO remove
    std::span<const VkPushConstantRange> m_pushConstantRanges;


    /// Bind this object as a root shader object
    Result bindAsRoot(
        RootShaderObject* shaderObject,
        RootShaderObjectLayoutImpl* specializedLayout,
        BindingDataImpl*& outBindingData
    );

    /// Bind this shader object as an entry point
    Result bindAsEntryPoint(
        ShaderObject* shaderObject,
        const BindingOffset& inOffset,
        EntryPointLayout* specializedLayout,
        uint32_t entryPointIndex,
        const DescriptorSetPlacement* placement
    );

    /// Bind this object as a `PushConstantBuffer<X>`.
    Result bindAsPushConstantBuffer(
        ShaderObject* shaderObject,
        const BindingOffset& inOffset,
        ShaderObjectLayoutImpl* specializedLayout,
        const DescriptorSetPlacement* placement
    );

    /// Bind the ordinary data buffer if needed.
    Result bindOrdinaryDataBufferIfNeeded(
        ShaderObject* shaderObject,
        BindingOffset& ioOffset,
        ShaderObjectLayoutImpl* specializedLayout
    );

    /// Bind this shader object as a "value"
    ///
    /// This is the mode used for binding sub-objects for existential-type
    /// fields, and is also used as part of the implementation of the
    /// parameter-block and constant-buffer cases.
    ///
    Result bindAsValue(
        ShaderObject* shaderObject,
        const BindingOffset& offset,
        ShaderObjectLayoutImpl* specializedLayout,
        const DescriptorSetPlacement* placement
    );

    /// Allocate the descriptor sets needed for binding this object (but not nested parameter
    /// blocks)
    Result allocateDescriptorSets(
        ShaderObject* shaderObject,
        const BindingOffset& offset,
        ShaderObjectLayoutImpl* specializedLayout
    );

    /// Bind this object as a `ParameterBlock<X>`.
    Result bindAsParameterBlock(
        ShaderObject* shaderObject,
        const BindingOffset& inOffset,
        ShaderObjectLayoutImpl* specializedLayout,
        const DescriptorSetPlacement* placement
    );

    /// Bind this object as a `ConstantBuffer<X>`.
    Result bindAsConstantBuffer(
        ShaderObject* shaderObject,
        const BindingOffset& inOffset,
        ShaderObjectLayoutImpl* specializedLayout,
        const DescriptorSetPlacement* placement
    );
};

struct BindingDataImpl : BindingData
{
public:
    struct BufferState
    {
        BufferImpl* buffer;
        ResourceState state;
    };
    struct TextureState
    {
        TextureViewImpl* textureView;
        ResourceState state;
    };
    /// Entry point data for copying to shader binding table (ray tracing)
    struct EntryPointData
    {
        // Host memory pointer to entry point uniform data
        void* data;
        // Size of the data in bytess
        size_t size;
    };

    /// Required buffer states.
    BufferState* bufferStates;
    uint32_t bufferStateCapacity;
    uint32_t bufferStateCount;
    /// Required texture states.
    TextureState* textureStates;
    uint32_t textureStateCapacity;
    uint32_t textureStateCount;

    /// Pipeline layout.
    VkPipelineLayout pipelineLayout;

    /// Descriptor sets.
    DescriptorSetBinding* descriptorSets;
    uint32_t heapPushOffset;
    uint32_t descriptorSetCount;

    /// Push constants.
    VkPushConstantRange* pushConstantRanges;
    void** pushConstantData;
    uint32_t pushConstantCount;

    /// Entry point data (for ray tracing SBT).
    EntryPointData* entryPointData;
    uint32_t entryPointCount;
};

struct BindingCache
{
    std::vector<BindingDataImpl*> bindingData;

    DescriptorHeap* heap = nullptr;
    std::vector<DescriptorHeap::Allocation> heapAllocations;
    void reset()
    {
        for (const auto& allocation : heapAllocations)
            heap->free(allocation);
        heapAllocations.clear();
        bindingData.clear();
    }
};

} // namespace rhi::vk
