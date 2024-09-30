#pragma once

#include "vk-base.h"
#include "vk-helper-functions.h"
#include "vk-texture-view.h"
#include "vk-sampler.h"
#include "vk-shader-object-layout.h"

#include "../state-tracking.h"

#include <vector>

namespace rhi::vk {

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
    static Result create(IDevice* device, ShaderObjectLayoutImpl* layout, ShaderObjectImpl** outShaderObject);

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

    Result init(IDevice* device, ShaderObjectLayoutImpl* layout);

    /// Write the uniform/ordinary data of this object into the given `dest` buffer at the given
    /// `offset`
    Result _writeOrdinaryData(
        CommandEncoderImpl* encoder,
        IBuffer* buffer,
        Offset offset,
        Size destSize,
        ShaderObjectLayoutImpl* specializedLayout
    );

public:
    /// Write a single descriptor using the Vulkan API
    static void writeDescriptor(RootBindingContext& context, VkWriteDescriptorSet const& write);

    static void writeBufferDescriptor(
        RootBindingContext& context,
        BindingOffset const& offset,
        VkDescriptorType descriptorType,
        BufferImpl* buffer,
        Offset bufferOffset,
        Size bufferSize
    );

    static void writeBufferDescriptor(
        RootBindingContext& context,
        BindingOffset const& offset,
        VkDescriptorType descriptorType,
        BufferImpl* buffer
    );

    static void writePlainBufferDescriptor(
        RootBindingContext& context,
        BindingOffset const& offset,
        VkDescriptorType descriptorType,
        span<ResourceSlot> slots
    );

    static void writeTexelBufferDescriptor(
        RootBindingContext& context,
        BindingOffset const& offset,
        VkDescriptorType descriptorType,
        span<ResourceSlot> slots
    );

    static void writeTextureSamplerDescriptor(
        RootBindingContext& context,
        BindingOffset const& offset,
        VkDescriptorType descriptorType,
        span<CombinedTextureSamplerSlot> slots
    );

    static void writeAccelerationStructureDescriptor(
        RootBindingContext& context,
        BindingOffset const& offset,
        VkDescriptorType descriptorType,
        span<ResourceSlot> slots
    );

    static void writeTextureDescriptor(
        RootBindingContext& context,
        BindingOffset const& offset,
        VkDescriptorType descriptorType,
        span<ResourceSlot> slots
    );

    static void writeSamplerDescriptor(
        RootBindingContext& context,
        BindingOffset const& offset,
        VkDescriptorType descriptorType,
        span<RefPtr<SamplerImpl>> samplers
    );

    bool shouldAllocateConstantBuffer(TransientResourceHeapImpl* transientHeap);

    /// Ensure that the `m_ordinaryDataBuffer` has been created, if it is needed
    Result _ensureOrdinaryDataBufferCreatedIfNeeded(
        CommandEncoderImpl* encoder,
        ShaderObjectLayoutImpl* specializedLayout
    );

public:
    /// Bind this shader object as a "value"
    ///
    /// This is the mode used for binding sub-objects for existential-type
    /// fields, and is also used as part of the implementation of the
    /// parameter-block and constant-buffer cases.
    ///
    Result bindAsValue(
        CommandEncoderImpl* encoder,
        RootBindingContext& context,
        BindingOffset const& offset,
        ShaderObjectLayoutImpl* specializedLayout
    );

    /// Allocate the descriptor sets needed for binding this object (but not nested parameter
    /// blocks)
    Result allocateDescriptorSets(
        CommandEncoderImpl* encoder,
        RootBindingContext& context,
        BindingOffset const& offset,
        ShaderObjectLayoutImpl* specializedLayout
    );

    /// Bind this object as a `ParameterBlock<X>`.
    Result bindAsParameterBlock(
        CommandEncoderImpl* encoder,
        RootBindingContext& context,
        BindingOffset const& inOffset,
        ShaderObjectLayoutImpl* specializedLayout
    );

    /// Bind the ordinary data buffer if needed.
    Result bindOrdinaryDataBufferIfNeeded(
        CommandEncoderImpl* encoder,
        RootBindingContext& context,
        BindingOffset& ioOffset,
        ShaderObjectLayoutImpl* specializedLayout
    );

    /// Bind this object as a `ConstantBuffer<X>`.
    Result bindAsConstantBuffer(
        CommandEncoderImpl* encoder,
        RootBindingContext& context,
        BindingOffset const& inOffset,
        ShaderObjectLayoutImpl* specializedLayout
    );

    void setResourceStates(StateTracking& stateTracking);

    std::vector<ResourceSlot> m_resources;
    std::vector<RefPtr<SamplerImpl>> m_samplers;
    std::vector<CombinedTextureSamplerSlot> m_combinedTextureSamplers;
    std::vector<RefPtr<AccelerationStructureImpl>> m_accelerationStructures;

    // The transient constant buffer that holds the GPU copy of the constant data,
    // weak referenced.
    IBuffer* m_constantBuffer = nullptr;
    // The offset into the transient constant buffer where the constant data starts.
    Offset m_constantBufferOffset = 0;
    Size m_constantBufferSize = 0;

    /// Dirty bit tracking whether the constant buffer needs to be updated.
    bool m_isConstantBufferDirty = true;
    /// The transient heap from which the constant buffer is allocated.
    TransientResourceHeapImpl* m_constantBufferTransientHeap;
    /// The version of the transient heap when the constant buffer is allocated.
    uint64_t m_constantBufferTransientHeapVersion;

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
    static Result create(IDevice* device, EntryPointLayout* layout, EntryPointShaderObject** outShaderObject);

    EntryPointLayout* getLayout();

    /// Bind this shader object as an entry point
    Result bindAsEntryPoint(
        CommandEncoderImpl* encoder,
        RootBindingContext& context,
        BindingOffset const& inOffset,
        EntryPointLayout* layout
    );

protected:
    Result init(IDevice* device, EntryPointLayout* layout);
};

class RootShaderObjectImpl : public ShaderObjectImpl
{
    using Super = ShaderObjectImpl;

public:
    // Override default reference counting behavior to disable lifetime management.
    // Root objects are managed by command buffer and does not need to be freed by the user.
    virtual SLANG_NO_THROW uint32_t SLANG_MCALL addRef() override { return 1; }
    virtual SLANG_NO_THROW uint32_t SLANG_MCALL release() override { return 1; }

public:
    RootShaderObjectLayout* getLayout();

    RootShaderObjectLayout* getSpecializedLayout();

    std::vector<RefPtr<EntryPointShaderObject>> const& getEntryPoints() const;

    virtual GfxCount SLANG_MCALL getEntryPointCount() override;
    virtual Result SLANG_MCALL getEntryPoint(GfxIndex index, IShaderObject** outEntryPoint) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    copyFrom(IShaderObject* object, ITransientResourceHeap* transientHeap) override;

    void setResourceStates(StateTracking& stateTracking);

    /// Bind this object as a root shader object
    Result bindAsRoot(CommandEncoderImpl* encoder, RootBindingContext& context, RootShaderObjectLayout* layout);

    virtual Result collectSpecializationArgs(ExtendedShaderObjectTypeList& args) override;

public:
    Result init(IDevice* device, RootShaderObjectLayout* layout);

protected:
    virtual Result _createSpecializedLayout(ShaderObjectLayoutImpl** outLayout) override;

    std::vector<RefPtr<EntryPointShaderObject>> m_entryPoints;
};

class MutableRootShaderObjectImpl : public RootShaderObjectImpl
{
public:
    // Enable reference counting.
    SLANG_NO_THROW uint32_t SLANG_MCALL addRef() override { return ShaderObjectImpl::addRef(); }
    SLANG_NO_THROW uint32_t SLANG_MCALL release() override { return ShaderObjectImpl::release(); }
};

} // namespace rhi::vk
