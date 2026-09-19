#pragma once

#include "vk-base.h"
#include "vk-shader-object-layout.h"
#include "../transient-buffer-heap.h"

#include "core/short_vector.h"

#include <vector>

namespace rhi::vk {

/// Uniform bytes with a block-relative push-constant range index. Root assembly resolves
/// the pipeline's byte offset and stage visibility after all blocks have been composed.
struct PushConstantBinding
{
    uint32_t rangeIndex;
    uint32_t size;
    void* data;
};

struct ParameterBlockBindingData;

struct BindingDataBuilder
{
    std::set<RefPtr<RefObject>>* m_resources = nullptr;
    bool m_buildingRoot = false;
    std::vector<VulkanDescriptorSet>* m_persistentDescriptorSets = nullptr;
    DeviceImpl* m_device;
    ArenaAllocator* m_allocator;
    BindingCache* m_bindingCache;
    BindingDataImpl* m_bindingData;
    TransientBufferArena* m_constantBufferArena;
    DescriptorSetAllocator* m_descriptorSetAllocator;

    short_vector<PushConstantBinding> m_pushConstants;


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
        uint32_t entryPointIndex
    );

    /// Bind this object as a `PushConstantBuffer<X>`.
    Result bindAsPushConstantBuffer(
        ShaderObject* shaderObject,
        const BindingOffset& inOffset,
        ShaderObjectLayoutImpl* specializedLayout
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
        ShaderObjectLayoutImpl* specializedLayout
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
        ShaderObjectLayoutImpl* specializedLayout
    );
    Result bindAsParameterBlockImpl(
        ShaderObject* shaderObject,
        const BindingOffset& inOffset,
        ShaderObjectLayoutImpl* specializedLayout
    );

    Result prepareParameterBlock(
        ShaderObject* shaderObject,
        ShaderObjectLayoutImpl* specializedLayout,
        const ParameterBlockBindingData*& outData
    );
    void composeParameterBlock(const ParameterBlockBindingData& data, const BindingOffset& offset);
    Result resolvePushConstants(std::span<const VkPushConstantRange> ranges);

    /// Bind this object as a `ConstantBuffer<X>`.
    Result bindAsConstantBuffer(
        ShaderObject* shaderObject,
        const BindingOffset& inOffset,
        ShaderObjectLayoutImpl* specializedLayout
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
    VkDescriptorSet* descriptorSets;
    uint32_t descriptorSetCount;

    /// Push constants.
    VkPushConstantRange* pushConstantRanges;
    void** pushConstantData;
    uint32_t pushConstantCount;

    /// Entry point data (for ray tracing SBT).
    EntryPointData* entryPointData;
    uint32_t entryPointCount;
};

struct ParameterBlockBindingData
{
    std::span<const VkDescriptorSet> descriptorSets;
    std::span<const PushConstantBinding> pushConstants;
    std::span<const BindingDataImpl::BufferState> bufferStates;
    std::span<const BindingDataImpl::TextureState> textureStates;
};

struct BindingCache
{
    std::vector<BindingDataImpl*> bindingData;

    void reset() { bindingData.clear(); }
};

} // namespace rhi::vk
