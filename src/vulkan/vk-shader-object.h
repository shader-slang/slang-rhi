#pragma once

#include "vk-base.h"
#include "vk-helper-functions.h"
#include "vk-buffer.h"
#include "vk-texture.h"
#include "vk-sampler.h"
#include "vk-shader-object-layout.h"
#include "vk-acceleration-structure.h"

#include "../state-tracking.h"

#include "core/short_vector.h"

#include <vector>

namespace rhi::vk {

struct BindableRootShaderObject
{
    /// Root object that this bindable object is associated with.
    RootShaderObjectImpl* rootObject;
    /// Vulkan pPipeline layout of the associated pipeline.
    VkPipelineLayout pipelineLayout;
    /// Descriptor sets that represent all the bindings the root object.
    short_vector<VkDescriptorSet> descriptorSets;
    struct PushConstant
    {
        VkPushConstantRange range;
        void* data;
    };
    /// Push constants that represent all the push constants the root object.
    short_vector<PushConstant> pushConstants;
};

/// Context information required when binding shader objects to the pipeline
struct BindingContext
{
    BindableRootShaderObject* bindable;

    /// The pipeline layout being used for binding
    // VkPipelineLayout pipelineLayout;

    /// The device being used
    DeviceImpl* device;

    /// An allocator to use for descriptor sets during binding
    DescriptorSetAllocator* descriptorSetAllocator;

    /// Transient resource heap for allocating transient resources (constant buffers)
    // TransientResourceHeapImpl* transientHeap;

    /// The descriptor sets that are being allocated and bound
    // std::vector<VkDescriptorSet>* descriptorSets;

    /// Information about all the push-constant ranges that should be bound
    span<const VkPushConstantRange> pushConstantRanges;

    virtual Result allocateConstantBuffer(size_t size, BufferImpl*& outBufferWeakPtr, size_t& outOffset) = 0;
    virtual void writeBuffer(BufferImpl* buffer, size_t offset, size_t size, void const* data) = 0;
    // virtual void writePushConstants(VkPushConstantRange range, const void* data) = 0;
};

struct ResourceSlot
{
    BindingType type = BindingType::Unknown;
    RefPtr<Resource> resource;
    Format format = Format::Unknown;
    union
    {
        BufferRange bufferRange = kEntireBuffer;
    };
    ResourceState requiredState = ResourceState::Undefined;
    operator bool() const { return type != BindingType::Unknown && resource; }
};

struct CombinedTextureSamplerSlot
{
    RefPtr<TextureViewImpl> textureView;
    RefPtr<SamplerImpl> sampler;
    operator bool() const { return textureView && sampler; }
};

class ShaderObjectImpl : public ShaderObjectBaseImpl<ShaderObjectImpl, ShaderObjectLayoutImpl, SimpleShaderObjectData>
{
public:
    static Result create(DeviceImpl* device, ShaderObjectLayoutImpl* layout, ShaderObjectImpl** outShaderObject);

    Device* getDevice();

    virtual SLANG_NO_THROW GfxCount SLANG_MCALL getEntryPointCount() override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getEntryPoint(GfxIndex index, IShaderObject** outEntryPoint) override;

    virtual SLANG_NO_THROW const void* SLANG_MCALL getRawData() override;

    virtual SLANG_NO_THROW Size SLANG_MCALL getSize() override;

    // TODO: Changed size_t to Size? inSize assigned to an Index variable inside implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL
    setData(ShaderOffset const& inOffset, void const* data, size_t inSize) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL setBinding(ShaderOffset const& offset, Binding binding) override;

protected:
    friend class RootShaderObjectLayout;

    Result init(DeviceImpl* device, ShaderObjectLayoutImpl* layout);

    /// Write the uniform/ordinary data of this object into the given `dest` buffer at the given
    /// `offset`
    Result _writeOrdinaryData(
        BindingContext& context,
        BufferImpl* buffer,
        Offset offset,
        Size destSize,
        ShaderObjectLayoutImpl* specializedLayout
    ) const;

public:
    /// Write a single descriptor using the Vulkan API
    static void writeDescriptor(BindingContext& context, VkWriteDescriptorSet const& write);

    static void writeBufferDescriptor(
        BindingContext& context,
        BindingOffset const& offset,
        VkDescriptorType descriptorType,
        BufferImpl* buffer,
        Offset bufferOffset,
        Size bufferSize
    );

    static void writeBufferDescriptor(
        BindingContext& context,
        BindingOffset const& offset,
        VkDescriptorType descriptorType,
        BufferImpl* buffer
    );

    static void writePlainBufferDescriptor(
        BindingContext& context,
        BindingOffset const& offset,
        VkDescriptorType descriptorType,
        span<const ResourceSlot> slots
    );

    static void writeTexelBufferDescriptor(
        BindingContext& context,
        BindingOffset const& offset,
        VkDescriptorType descriptorType,
        span<const ResourceSlot> slots
    );

    static void writeTextureSamplerDescriptor(
        BindingContext& context,
        BindingOffset const& offset,
        VkDescriptorType descriptorType,
        span<const CombinedTextureSamplerSlot> slots
    );

    static void writeAccelerationStructureDescriptor(
        BindingContext& context,
        BindingOffset const& offset,
        VkDescriptorType descriptorType,
        span<const ResourceSlot> slots
    );

    static void writeTextureDescriptor(
        BindingContext& context,
        BindingOffset const& offset,
        VkDescriptorType descriptorType,
        span<const ResourceSlot> slots
    );

    static void writeSamplerDescriptor(
        BindingContext& context,
        BindingOffset const& offset,
        VkDescriptorType descriptorType,
        span<const RefPtr<SamplerImpl>> samplers
    );

    /// Ensure that the `m_ordinaryDataBuffer` has been created, if it is needed
    Result _ensureOrdinaryDataBufferCreatedIfNeeded(BindingContext& context, ShaderObjectLayoutImpl* specializedLayout)
        const;

public:
    /// Bind this shader object as a "value"
    ///
    /// This is the mode used for binding sub-objects for existential-type
    /// fields, and is also used as part of the implementation of the
    /// parameter-block and constant-buffer cases.
    ///
    Result bindAsValue(BindingContext& context, BindingOffset const& offset, ShaderObjectLayoutImpl* specializedLayout)
        const;

    /// Allocate the descriptor sets needed for binding this object (but not nested parameter
    /// blocks)
    Result allocateDescriptorSets(
        BindingContext& context,
        BindingOffset const& offset,
        ShaderObjectLayoutImpl* specializedLayout
    ) const;

    /// Bind this object as a `ParameterBlock<X>`.
    Result bindAsParameterBlock(
        BindingContext& context,
        BindingOffset const& inOffset,
        ShaderObjectLayoutImpl* specializedLayout
    ) const;

    /// Bind the ordinary data buffer if needed.
    Result bindOrdinaryDataBufferIfNeeded(
        BindingContext& context,
        BindingOffset& ioOffset,
        ShaderObjectLayoutImpl* specializedLayout
    ) const;

    /// Bind this object as a `ConstantBuffer<X>`.
    Result bindAsConstantBuffer(
        BindingContext& context,
        BindingOffset const& inOffset,
        ShaderObjectLayoutImpl* specializedLayout
    ) const;

    void setResourceStates(StateTracking& stateTracking) const;

    std::vector<ResourceSlot> m_resources;
    std::vector<RefPtr<SamplerImpl>> m_samplers;
    std::vector<CombinedTextureSamplerSlot> m_combinedTextureSamplers;
    std::vector<RefPtr<AccelerationStructureImpl>> m_accelerationStructures;

    /// Get the layout of this shader object with specialization arguments considered
    ///
    /// This operation should only be called after the shader object has been
    /// fully filled in and finalized.
    ///
    Result _getSpecializedLayout(ShaderObjectLayoutImpl** outLayout);

    /// Create the layout for this shader object with specialization arguments considered
    ///
    /// This operation is virtual so that it can be customized by `ProgramVars`.
    ///
    virtual Result _createSpecializedLayout(ShaderObjectLayoutImpl** outLayout);

    RefPtr<ShaderObjectLayoutImpl> m_specializedLayout;
};

class EntryPointShaderObject : public ShaderObjectImpl
{
    typedef ShaderObjectImpl Super;

public:
    static Result create(DeviceImpl* device, EntryPointLayout* layout, EntryPointShaderObject** outShaderObject);

    EntryPointLayout* getLayout();

    /// Bind this shader object as an entry point
    Result bindAsEntryPoint(BindingContext& context, BindingOffset const& inOffset, EntryPointLayout* layout);

protected:
    Result init(DeviceImpl* device, EntryPointLayout* layout);
};

class RootShaderObjectImpl : public ShaderObjectImpl
{
    using Super = ShaderObjectImpl;

public:
    RootShaderObjectLayout* getLayout();

    RootShaderObjectLayout* getSpecializedLayout();

    std::vector<RefPtr<EntryPointShaderObject>> const& getEntryPoints() const;

    virtual GfxCount SLANG_MCALL getEntryPointCount() override;
    virtual Result SLANG_MCALL getEntryPoint(GfxIndex index, IShaderObject** outEntryPoint) override;

    void setResourceStates(StateTracking& stateTracking);

    /// Bind this object as a root shader object
    Result bindAsRoot(BindingContext& context, RootShaderObjectLayout* layout);

    virtual Result collectSpecializationArgs(ExtendedShaderObjectTypeList& args) override;

public:
    Result init(DeviceImpl* device, RootShaderObjectLayout* layout);

protected:
    virtual Result _createSpecializedLayout(ShaderObjectLayoutImpl** outLayout) override;

    std::vector<RefPtr<EntryPointShaderObject>> m_entryPoints;
};

} // namespace rhi::vk
