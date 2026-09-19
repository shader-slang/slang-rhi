#pragma once

#include "vk-base.h"
#include "vk-shader-object-layout.h"
#include "../binding-data-storage.h"

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
struct PreparedBindingData;

/// Transient sets belong to the command-buffer pool; persistent sets are returned individually
/// to the device allocator when their prepared owner is retired.
class BindingDataStorage : public rhi::BindingDataStorage
{
public:
    explicit BindingDataStorage(CommandBufferImpl& commandBuffer);
    explicit BindingDataStorage(PreparedBindingData& prepared);

    DeviceImpl* getDevice() const { return m_device; }
    BindingDataImpl* allocateBindingData();
    Result allocateDescriptorSet(VkDescriptorSetLayout layout, VkDescriptorSet& outSet);

private:
    DeviceImpl* m_device;
    BindingCache& m_bindingCache;
    DescriptorSetAllocator& m_descriptorSetAllocator;
    std::vector<VulkanDescriptorSet>* m_persistentDescriptorSets = nullptr;
};

struct BindingDataBuilder
{
    explicit BindingDataBuilder(BindingDataStorage& storage)
        : m_storage(storage)
        , m_device(storage.getDevice())
    {
    }
    BindingDataBuilder(const BindingDataBuilder&) = delete;
    BindingDataBuilder& operator=(const BindingDataBuilder&) = delete;

    BindingDataStorage& m_storage;
    DeviceImpl* const m_device;
    BindingDataImpl* m_bindingData = nullptr;

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

private:
    Result bindAsRootImpl(
        RootShaderObject* shaderObject,
        RootShaderObjectLayoutImpl* specializedLayout,
        BindingDataImpl*& outBindingData
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

/// Owns persistent allocations, including partially built data on preparation failure.
struct PreparedBindingData : PreparedShaderObject
{
    ~PreparedBindingData();
    void init(DeviceImpl* deviceImpl) { device = deviceImpl; }

    DeviceImpl* device = nullptr;
    BindingDataImpl* bindingData = nullptr;
    BindingCache bindingCache;
    std::vector<VulkanDescriptorSet> descriptorSets;
};

} // namespace rhi::vk
