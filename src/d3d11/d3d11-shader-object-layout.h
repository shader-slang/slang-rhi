#pragma once

#include "d3d11-base.h"

namespace rhi::d3d11 {

// In order to bind shader parameters to the correct locations, we need to
// be able to describe those locations. Most shader parameters will
// only consume a single type of D3D11-visible regsiter (e.g., a `t`
// register for a texture, or an `s` register for a sampler), and scalar
// integers suffice for these cases.
//
// In more complex cases we might be binding an entire "sub-object" like
// a parameter block, an entry point, etc. For the general case, we need
// to be able to represent a composite offset that includes offsets for
// each of the register classes known to D3D11.

/// A "simple" binding offset that records an offset in CBV/SRV/UAV/Sampler slots
struct SimpleBindingOffset
{
    uint32_t cbv = 0;
    uint32_t srv = 0;
    uint32_t uav = 0;
    uint32_t sampler = 0;

    /// Create a default (zero) offset
    SimpleBindingOffset() {}

    /// Create an offset based on offset information in the given Slang `varLayout`
    SimpleBindingOffset(slang::VariableLayoutReflection* varLayout)
    {
        if (varLayout)
        {
            cbv = (uint32_t)varLayout->getOffset(SLANG_PARAMETER_CATEGORY_CONSTANT_BUFFER);
            srv = (uint32_t)varLayout->getOffset(SLANG_PARAMETER_CATEGORY_SHADER_RESOURCE);
            uav = (uint32_t)varLayout->getOffset(SLANG_PARAMETER_CATEGORY_UNORDERED_ACCESS);
            sampler = (uint32_t)varLayout->getOffset(SLANG_PARAMETER_CATEGORY_SAMPLER_STATE);
        }
    }

    /// Create an offset based on size/stride information in the given Slang `typeLayout`
    SimpleBindingOffset(slang::TypeLayoutReflection* typeLayout)
    {
        if (typeLayout)
        {
            cbv = (uint32_t)typeLayout->getSize(SLANG_PARAMETER_CATEGORY_CONSTANT_BUFFER);
            srv = (uint32_t)typeLayout->getSize(SLANG_PARAMETER_CATEGORY_SHADER_RESOURCE);
            uav = (uint32_t)typeLayout->getSize(SLANG_PARAMETER_CATEGORY_UNORDERED_ACCESS);
            sampler = (uint32_t)typeLayout->getSize(SLANG_PARAMETER_CATEGORY_SAMPLER_STATE);
        }
    }

    /// Add any values in the given `offset`
    void operator+=(const SimpleBindingOffset& offset)
    {
        cbv += offset.cbv;
        srv += offset.srv;
        uav += offset.uav;
        sampler += offset.sampler;
    }
};

/// A representation of the offset at which to bind a shader parameter or sub-object
struct BindingOffset : SimpleBindingOffset
{
    /// Create a default (zero) offset
    BindingOffset() {}

    /// Create an offset from a simple offset
    explicit BindingOffset(const SimpleBindingOffset& offset)
        : SimpleBindingOffset(offset)
    {
    }

    /// Create an offset based on offset information in the given Slang `varLayout`
    BindingOffset(slang::VariableLayoutReflection* varLayout)
        : SimpleBindingOffset(varLayout)
    {
    }

    /// Create an offset based on size/stride information in the given Slang `typeLayout`
    BindingOffset(slang::TypeLayoutReflection* typeLayout)
        : SimpleBindingOffset(typeLayout)
    {
    }

    /// Add any values in the given `offset`
    void operator+=(const SimpleBindingOffset& offset) { SimpleBindingOffset::operator+=(offset); }

    /// Add any values in the given `offset`
    void operator+=(const BindingOffset& offset) { SimpleBindingOffset::operator+=(offset); }
};

class ShaderObjectLayoutImpl : public ShaderObjectLayout
{
    using Super = ShaderObjectLayout;

public:
    // A shader object comprises three main kinds of state:
    //
    // * Zero or more bytes of ordinary ("uniform") data
    // * Zero or more *bindings* for textures, buffers, and samplers
    // * Zero or more *sub-objects* representing nested parameter blocks, etc.
    //
    // A shader object *layout* stores information that can be used to
    // organize these different kinds of state and optimize access to them.
    //
    // For example, both texture/buffer/sampler bindings and sub-objects
    // are organized into logical *binding ranges* by the Slang reflection
    // API, and a shader object layout will store information about those
    // ranges in a form that is usable for the D3D11 API:

    /// Information about a logical binding range as reported by Slang reflection
    struct BindingRangeInfo : Super::BindingRangeInfo
    {
        /// The offset of this binding range from the start of the sub-object
        /// in terms of whatever D3D11 register class it consumes. E.g., for
        /// a `Texture2D` binding range this will represent an offset in
        /// `t` registers.
        ///
        uint32_t registerOffset;
    };

    // Sometimes we just want to iterate over the ranges that represent
    // sub-objects while skipping over the others, because sub-object
    // ranges often require extra handling or more state.
    //
    // For that reason we also store pre-computed information about each
    // sub-object range.

    /// Offset information for a sub-object range
    struct SubObjectRangeOffset : BindingOffset
    {
        SubObjectRangeOffset() {}

        SubObjectRangeOffset(slang::VariableLayoutReflection* varLayout);
    };

    /// Stride information for a sub-object range
    struct SubObjectRangeStride : BindingOffset
    {
        SubObjectRangeStride() {}

        SubObjectRangeStride(slang::TypeLayoutReflection* typeLayout);
    };

    /// Information about a logical binding range as reported by Slang reflection
    struct SubObjectRangeInfo : Super::SubObjectRangeInfo
    {
        /// The offset to use when binding the first object in this range
        SubObjectRangeOffset offset;

        /// Stride between consecutive objects in this range
        SubObjectRangeStride stride;

        /// The layout expected for objects bound to this range (if known)
        RefPtr<ShaderObjectLayoutImpl> layout;
    };

    std::vector<BindingRangeInfo> m_bindingRanges;
    std::vector<SubObjectRangeInfo> m_subObjectRanges;

    uint32_t m_slotCount = 0;
    uint32_t m_subObjectCount = 0;
    SimpleBindingOffset m_resourceCount;
    SimpleBindingOffset m_totalResourceCount;

    uint32_t m_totalOrdinaryDataSize = 0;

    static Result createForElementType(
        Device* device,
        slang::ISession* session,
        slang::TypeLayoutReflection* elementType,
        ShaderObjectLayoutImpl** outLayout
    );

    // ShaderObjectLayout interface
    virtual uint32_t getSlotCount() const override { return m_slotCount; }
    virtual uint32_t getSubObjectCount() const override { return m_subObjectCount; };

    virtual uint32_t getBindingRangeCount() const override { return m_bindingRanges.size(); }
    virtual const BindingRangeInfo& getBindingRange(uint32_t index) const override { return m_bindingRanges[index]; }

    virtual uint32_t getSubObjectRangeCount() const override { return m_subObjectRanges.size(); }
    virtual const SubObjectRangeInfo& getSubObjectRange(uint32_t index) const override
    {
        return m_subObjectRanges[index];
    }
    virtual ShaderObjectLayout* getSubObjectRangeLayout(uint32_t index) const override
    {
        return m_subObjectRanges[index].layout;
    }

protected:
    struct Builder
    {
    public:
        Device* m_device;
        slang::ISession* m_session;
        slang::TypeLayoutReflection* m_elementTypeLayout;

        std::vector<BindingRangeInfo> m_bindingRanges;
        std::vector<SubObjectRangeInfo> m_subObjectRanges;

        uint32_t m_slotCount = 0;
        uint32_t m_subObjectCount = 0;
        SimpleBindingOffset m_resourceCount;
        SimpleBindingOffset m_totalResourceCount;

        uint32_t m_totalOrdinaryDataSize = 0;

        /// The container type of this shader object. When `m_containerType` is
        /// `StructuredBuffer` or `UnsizedArray`, this shader object represents a collection
        /// instead of a single object.
        ShaderObjectContainerType m_containerType = ShaderObjectContainerType::None;

        Builder(Device* device, slang::ISession* session)
            : m_device(device)
            , m_session(session)
        {
        }

        Result setElementTypeLayout(slang::TypeLayoutReflection* typeLayout);
        Result build(ShaderObjectLayoutImpl** outLayout);
    };

    Result _init(const Builder* builder);
};

class RootShaderObjectLayoutImpl : public ShaderObjectLayoutImpl
{
    using Super = ShaderObjectLayoutImpl;

public:
    struct EntryPointInfo : Super::EntryPointInfo
    {
        /// The offset for this entry point's parameters, relative to the starting offset for the program
        BindingOffset offset;

        RefPtr<ShaderObjectLayoutImpl> layout;
    };

    ComPtr<slang::IComponentType> m_program;
    slang::ProgramLayout* m_programLayout = nullptr;

    std::vector<EntryPointInfo> m_entryPoints;

    static Result create(
        Device* device,
        slang::IComponentType* program,
        slang::ProgramLayout* programLayout,
        RootShaderObjectLayoutImpl** outLayout
    );

    // ShaderObjectLayout interface
    virtual uint32_t getEntryPointCount() const override { return m_entryPoints.size(); }
    virtual const EntryPointInfo& getEntryPoint(uint32_t index) const override { return m_entryPoints[index]; }
    virtual ShaderObjectLayout* getEntryPointLayout(uint32_t index) const override
    {
        return m_entryPoints[index].layout;
    }

protected:
    struct Builder : Super::Builder
    {
        slang::IComponentType* m_program;
        slang::ProgramLayout* m_programLayout;
        std::vector<EntryPointInfo> m_entryPoints;

        Builder(Device* device, slang::IComponentType* program, slang::ProgramLayout* programLayout)
            : Super::Builder(device, program->getSession())
            , m_program(program)
            , m_programLayout(programLayout)
        {
        }

        Result build(RootShaderObjectLayoutImpl** outLayout);
        void addGlobalParams(slang::VariableLayoutReflection* globalsLayout);
        void addEntryPoint(
            SlangStage stage,
            ShaderObjectLayoutImpl* entryPointLayout,
            slang::EntryPointLayout* slangEntryPoint
        );
    };

    Result _init(const Builder* builder);
};

} // namespace rhi::d3d11
