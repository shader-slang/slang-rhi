#pragma once

#include "d3d11-base.h"
#include "d3d11-helper-functions.h"

namespace rhi::d3d11 {

// In order to bind shader parameters to the correct locations, we need to
// be able to describe those locations. Most shader parameters will
// only consume a single type of D3D11-visible regsiter (e.g., a `t`
// register for a txture, or an `s` register for a sampler), and scalar
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

// While a "simple" binding offset representation will work in many cases,
// once we need to deal with layout for programs with interface-type parameters
// that have been statically specialized, we also need to track the offset
// for where to bind any "pending" data that arises from the process of static
// specialization.
//
// In order to conveniently track both the "primary" and "pending" offset information,
// we will define a more complete `BindingOffset` type that combines simple
// binding offsets for the primary and pending parts.

/// A representation of the offset at which to bind a shader parameter or sub-object
struct BindingOffset : SimpleBindingOffset
{
    // Offsets for "primary" data are stored directly in the `BindingOffset`
    // via the inheritance from `SimpleBindingOffset`.

    /// Offset for any "pending" data
    SimpleBindingOffset pending;

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
        , pending(varLayout->getPendingDataLayout())
    {
    }

    /// Create an offset based on size/stride information in the given Slang `typeLayout`
    BindingOffset(slang::TypeLayoutReflection* typeLayout)
        : SimpleBindingOffset(typeLayout)
        , pending(typeLayout->getPendingDataTypeLayout())
    {
    }

    /// Add any values in the given `offset`
    void operator+=(const SimpleBindingOffset& offset) { SimpleBindingOffset::operator+=(offset); }

    /// Add any values in the given `offset`
    void operator+=(const BindingOffset& offset)
    {
        SimpleBindingOffset::operator+=(offset);
        pending += offset.pending;
    }
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
    struct BindingRangeInfo : public Super::BindingRangeInfo
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

        /// The offset for "pending" ordinary data related to this range
        uint32_t pendingOrdinaryData = 0;
    };

    /// Stride information for a sub-object range
    struct SubObjectRangeStride : BindingOffset
    {
        SubObjectRangeStride() {}

        SubObjectRangeStride(slang::TypeLayoutReflection* typeLayout);

        /// The strid for "pending" ordinary data related to this range
        uint32_t pendingOrdinaryData = 0;
    };

    /// Information about a logical binding range as reported by Slang reflection
    struct SubObjectRangeInfo : public Super::SubObjectRangeInfo
    {
        /// The offset to use when binding the first object in this range
        SubObjectRangeOffset offset;

        /// Stride between consecutive objects in this range
        SubObjectRangeStride stride;
    };

    struct Builder
    {
    public:
        Builder(Device* device, slang::ISession* session)
            : m_device(device)
            , m_session(session)
        {
        }

        Device* m_device;
        slang::ISession* m_session;
        slang::TypeLayoutReflection* m_elementTypeLayout;

        std::vector<BindingRangeInfo> m_bindingRanges;
        std::vector<SubObjectRangeInfo> m_subObjectRanges;

        Index m_slotCount = 0;
        Index m_srvCount = 0;
        Index m_samplerCount = 0;
        Index m_uavCount = 0;
        Index m_subObjectCount = 0;

        uint32_t m_totalOrdinaryDataSize = 0;

        /// The container type of this shader object. When `m_containerType` is
        /// `StructuredBuffer` or `UnsizedArray`, this shader object represents a collection
        /// instead of a single object.
        ShaderObjectContainerType m_containerType = ShaderObjectContainerType::None;

        Result setElementTypeLayout(slang::TypeLayoutReflection* typeLayout);
        Result build(ShaderObjectLayoutImpl** outLayout);
    };

    static Result createForElementType(
        Device* device,
        slang::ISession* session,
        slang::TypeLayoutReflection* elementType,
        ShaderObjectLayoutImpl** outLayout
    );

    virtual Index getSlotCount() const override { return m_slotCount; }
    virtual Index getSubObjectCount() const override { return m_subObjectCount; };

    virtual Index getBindingRangeCount() const override { return m_bindingRanges.size(); }
    virtual const BindingRangeInfo& getBindingRange(Index index) const override { return m_bindingRanges[index]; }

    virtual Index getSubObjectRangeCount() const override { return m_subObjectRanges.size(); }
    virtual const SubObjectRangeInfo& getSubObjectRange(Index index) const override { return m_subObjectRanges[index]; }

    virtual Index getEntryPointCount() const override { return 0; }
    virtual const EntryPointInfo& getEntryPoint(Index index) const override { return *(EntryPointInfo*)(nullptr); }


    const std::vector<BindingRangeInfo>& getBindingRanges() { return m_bindingRanges; }

    Index getBindingRangeCount() { return m_bindingRanges.size(); }

    const BindingRangeInfo& getBindingRange(Index index) { return m_bindingRanges[index]; }

    Index getSlotCount() { return m_slotCount; }
    Index getSRVCount() { return m_srvCount; }
    Index getSamplerCount() { return m_samplerCount; }
    Index getUAVCount() { return m_uavCount; }
    Index getSubObjectCount() { return m_subObjectCount; }

    const SubObjectRangeInfo& getSubObjectRange(Index index) { return m_subObjectRanges[index]; }
    const std::vector<SubObjectRangeInfo>& getSubObjectRanges() { return m_subObjectRanges; }

    Device* getDevice() { return m_device; }

    slang::TypeReflection* getType() { return m_elementTypeLayout->getType(); }

    uint32_t getTotalOrdinaryDataSize() const { return m_totalOrdinaryDataSize; }

protected:
    Result _init(const Builder* builder);

    std::vector<BindingRangeInfo> m_bindingRanges;
    std::vector<Index> m_srvRanges;
    std::vector<Index> m_uavRanges;
    std::vector<Index> m_samplerRanges;
    Index m_slotCount = 0;
    Index m_srvCount = 0;
    Index m_samplerCount = 0;
    Index m_uavCount = 0;
    Index m_subObjectCount = 0;
    uint32_t m_totalOrdinaryDataSize = 0;
    std::vector<SubObjectRangeInfo> m_subObjectRanges;
};

class RootShaderObjectLayoutImpl : public ShaderObjectLayoutImpl
{
    typedef ShaderObjectLayoutImpl Super;

public:
    struct EntryPointInfo
    {
        RefPtr<ShaderObjectLayoutImpl> layout;

        /// The offset for this entry point's parameters, relative to the starting offset for the program
        BindingOffset offset;
    };

    struct Builder : Super::Builder
    {
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

        slang::IComponentType* m_program;
        slang::ProgramLayout* m_programLayout;
        std::vector<EntryPointInfo> m_entryPoints;
        SimpleBindingOffset m_pendingDataOffset;
    };

    EntryPointInfo& getEntryPoint(Index index) { return m_entryPoints[index]; }

    std::vector<EntryPointInfo>& getEntryPoints() { return m_entryPoints; }

    static Result create(
        Device* device,
        slang::IComponentType* program,
        slang::ProgramLayout* programLayout,
        RootShaderObjectLayoutImpl** outLayout
    );

    slang::IComponentType* getSlangProgram() const { return m_program; }
    slang::ProgramLayout* getSlangProgramLayout() const { return m_programLayout; }

    /// Get the offset at which "pending" shader parameters for this program start
    const SimpleBindingOffset& getPendingDataOffset() const { return m_pendingDataOffset; }

protected:
    Result _init(const Builder* builder);

    ComPtr<slang::IComponentType> m_program;
    slang::ProgramLayout* m_programLayout = nullptr;

    std::vector<EntryPointInfo> m_entryPoints;
    SimpleBindingOffset m_pendingDataOffset;
};

} // namespace rhi::d3d11
