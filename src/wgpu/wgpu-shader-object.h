#pragma once

#include "wgpu-base.h"
#include "wgpu-shader-object-layout.h"
#include "wgpu-buffer.h"
#include "wgpu-texture.h"
#include "wgpu-sampler.h"

#include <vector>

namespace rhi::wgpu {

/// Context information required when binding shader objects to the pipeline
struct BindingContext
{
    /// The device being used
    DeviceImpl* device;

    span<WGPUBindGroupLayout> bindGroupLayouts;

    /// The bind group entries for every descriptor set
    std::vector<std::vector<WGPUBindGroupEntry>> entries;

    /// The descriptor sets that are being allocated and bound
    std::vector<WGPUBindGroup> bindGroups;
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
    operator bool() const { return type != BindingType::Unknown && resource; }
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
    Result _writeOrdinaryData(uint8_t* destData, Size destSize, ShaderObjectLayoutImpl* specializedLayout) const;

public:
    /// Write a single descriptor using the Vulkan API
    static void writeDescriptor(BindingContext& context, Index bindingSet, WGPUBindGroupEntry const& write);

    static void writeBufferDescriptor(
        BindingContext& context,
        BindingOffset const& offset,
        BufferImpl* buffer,
        Offset bufferOffset,
        Size bufferSize
    );

    static void writeBufferDescriptor(BindingContext& context, BindingOffset const& offset, BufferImpl* buffer);

    static void writeBufferDescriptor(
        BindingContext& context,
        BindingOffset const& offset,
        span<const ResourceSlot> slots
    );

    static void writeTextureDescriptor(
        BindingContext& context,
        BindingOffset const& offset,
        span<const ResourceSlot> slots
    );

    static void writeSamplerDescriptor(
        BindingContext& context,
        BindingOffset const& offset,
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

    Result createBindGroups(BindingContext& context) const;

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

    std::vector<ResourceSlot> m_resources;
    std::vector<RefPtr<SamplerImpl>> m_samplers;

    // The size of the constant buffer for this object.
    mutable Size m_constantBufferSize = 0;
    // The constant buffer containing all the ordinary data for this object.
    mutable RefPtr<BufferImpl> m_constantBuffer;
    // A CPU memory buffer containing the ordinary data for this object.
    mutable std::vector<uint8_t> m_constantBufferData;

    /// Dirty bit tracking whether the constant buffer needs to be updated.
    bool m_isConstantBufferDirty = true;

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
    Result bindAsEntryPoint(BindingContext& context, BindingOffset const& inOffset, EntryPointLayout* layout) const;

protected:
    Result init(DeviceImpl* device, EntryPointLayout* layout);
};

class RootShaderObjectImpl : public ShaderObjectImpl
{
    using Super = ShaderObjectImpl;

public:
    // Override default reference counting behavior to disable lifetime management.
    // Root objects are managed by command buffer and does not need to be freed by the user.
    // virtual SLANG_NO_THROW uint32_t SLANG_MCALL addRef() override { return 1; }
    // virtual SLANG_NO_THROW uint32_t SLANG_MCALL release() override { return 1; }

public:
    RootShaderObjectLayout* getLayout();

    RootShaderObjectLayout* getSpecializedLayout();

    std::vector<RefPtr<EntryPointShaderObject>> const& getEntryPoints() const;

    virtual GfxCount SLANG_MCALL getEntryPointCount() override;
    virtual Result SLANG_MCALL getEntryPoint(GfxIndex index, IShaderObject** outEntryPoint) override;

    /// Bind this object as a root shader object
    Result bindAsRoot(BindingContext& context, RootShaderObjectLayout* layout) const;

    virtual Result collectSpecializationArgs(ExtendedShaderObjectTypeList& args) override;

public:
    Result init(DeviceImpl* device, RootShaderObjectLayout* layout);

protected:
    virtual Result _createSpecializedLayout(ShaderObjectLayoutImpl** outLayout) override;

    std::vector<RefPtr<EntryPointShaderObject>> m_entryPoints;
};

} // namespace rhi::wgpu
