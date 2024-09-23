#pragma once

#include "wgpu-base.h"
#include "wgpu-shader-object-layout.h"

#include "core/static_vector.h"

#include <map>
#include <vector>

namespace rhi::wgpu {

enum
{
    kMaxDescriptorSets = 4,
};

// In order to bind shader parameters to the correct locations, we need to
// be able to describe those locations. Most shader parameters in Vulkan
// simply consume a single `binding`, but we also need to deal with
// parameters that represent push-constant ranges.
//
// In more complex cases we might be binding an entire "sub-object" like
// a parameter block, an entry point, etc. For the general case, we need
// to be able to represent a composite offset that includes offsets for
// each of the cases that Vulkan supports.

/// A "simple" binding offset that records `binding`, `set`, etc. offsets
struct SimpleBindingOffset
{
    /// An offset in WGSL `binding`s
    uint32_t binding = 0;

    /// The descriptor `set` that the `binding` field should be understood as an index into
    uint32_t bindingSet = 0;

    /// Create a default (zero) offset
    SimpleBindingOffset() {}

    /// Create an offset based on offset information in the given Slang `varLayout`
    SimpleBindingOffset(slang::VariableLayoutReflection* varLayout)
    {
        if (varLayout)
        {
            bindingSet = (uint32_t)varLayout->getBindingSpace(SLANG_PARAMETER_CATEGORY_DESCRIPTOR_TABLE_SLOT);
            binding = (uint32_t)varLayout->getOffset(SLANG_PARAMETER_CATEGORY_DESCRIPTOR_TABLE_SLOT);
        }
    }

    /// Add any values in the given `offset`
    void operator+=(SimpleBindingOffset const& offset)
    {
        binding += offset.binding;
        bindingSet += offset.bindingSet;
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
    explicit BindingOffset(SimpleBindingOffset const& offset)
        : SimpleBindingOffset(offset)
    {
    }

    /// Create an offset based on offset information in the given Slang `varLayout`
    BindingOffset(slang::VariableLayoutReflection* varLayout)
        : SimpleBindingOffset(varLayout)
        , pending(varLayout->getPendingDataLayout())
    {
    }

    /// Add any values in the given `offset`
    void operator+=(SimpleBindingOffset const& offset) { SimpleBindingOffset::operator+=(offset); }

    /// Add any values in the given `offset`
    void operator+=(BindingOffset const& offset)
    {
        SimpleBindingOffset::operator+=(offset);
        pending += offset.pending;
    }
};

class ShaderObjectLayoutImpl : public ShaderObjectLayout
{
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
    // ranges in a form that is usable for the Vulkan API:

    struct BindingRangeInfo
    {
        slang::BindingType bindingType;
        Index count;
        Index baseIndex;

        /// An index into the sub-object array if this binding range is treated
        /// as a sub-object.
        Index subObjectIndex;

        /// The `binding` offset to apply for this range
        uint32_t bindingOffset;

        /// The `set` offset to apply for this range
        uint32_t setOffset;

        // Note: The 99% case is that `setOffset` will be zero. For any shader object
        // that was allocated from an ordinary Slang type (anything other than a root
        // shader object in fact), all of the bindings will have been allocated into
        // a single logical descriptor set.
        //
        // TODO: Ideally we could refactor so that only the root shader object layout
        // stores a set offset for its binding ranges, and all other objects skip
        // storing a field that never actually matters.

        // Is this binding range representing a specialization point, such as
        // an existential value or a ParameterBlock<IFoo>.
        bool isSpecializable;
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

        SubObjectRangeOffset(slang::VariableLayoutReflection* varLayout)
            : BindingOffset(varLayout)
        {
            if (auto pendingLayout = varLayout->getPendingDataLayout())
            {
                pendingOrdinaryData = (uint32_t)pendingLayout->getOffset(SLANG_PARAMETER_CATEGORY_UNIFORM);
            }
        }

        /// The offset for "pending" ordinary data related to this range
        uint32_t pendingOrdinaryData = 0;
    };

    /// Stride information for a sub-object range
    struct SubObjectRangeStride : BindingOffset
    {
        SubObjectRangeStride() {}

        SubObjectRangeStride(slang::TypeLayoutReflection* typeLayout)
        {
            if (auto pendingLayout = typeLayout->getPendingDataTypeLayout())
            {
                pendingOrdinaryData = (uint32_t)pendingLayout->getStride();
            }
        }

        /// The stride for "pending" ordinary data related to this range
        uint32_t pendingOrdinaryData = 0;
    };

    /// Information about a logical binding range as reported by Slang reflection
    struct SubObjectRangeInfo
    {
        /// The index of the binding range that corresponds to this sub-object range
        Index bindingRangeIndex;

        /// The layout expected for objects bound to this range (if known)
        RefPtr<ShaderObjectLayoutImpl> layout;

        /// The offset to use when binding the first object in this range
        SubObjectRangeOffset offset;

        /// Stride between consecutive objects in this range
        SubObjectRangeStride stride;
    };

    struct DescriptorSetInfo
    {
        std::vector<WGPUBindGroupLayoutEntry> entries;
        int32_t space = -1;
        WGPUBindGroupLayout bindGroupLayout = nullptr;
    };

    struct Builder
    {
    public:
        Builder(DeviceImpl* device, slang::ISession* session)
            : m_device(device)
            , m_session(session)
        {
        }

        DeviceImpl* m_device;
        slang::ISession* m_session;
        slang::TypeLayoutReflection* m_elementTypeLayout;

        /// The container type of this shader object. When `m_containerType` is
        /// `StructuredBuffer` or `UnsizedArray`, this shader object represents a collection
        /// instead of a single object.
        ShaderObjectContainerType m_containerType = ShaderObjectContainerType::None;

        std::vector<BindingRangeInfo> m_bindingRanges;
        std::vector<SubObjectRangeInfo> m_subObjectRanges;

        Index m_resourceCount = 0;
        Index m_samplerCount = 0;
        Index m_subObjectCount = 0;
        Index m_varyingInputCount = 0;
        Index m_varyingOutputCount = 0;
        std::vector<DescriptorSetInfo> m_descriptorSetBuildInfos;
        std::map<Index, Index> m_mapSpaceToDescriptorSetIndex;

        /// The number of descriptor sets allocated by child/descendent objects
        uint32_t m_childDescriptorSetCount = 0;

        /// The total number of `binding`s consumed by this object and its children/descendents
        uint32_t m_totalBindingCount = 0;

        uint32_t m_totalOrdinaryDataSize = 0;

        Index findOrAddDescriptorSet(Index space);

        /// Add any descriptor ranges implied by this object containing a leaf
        /// sub-object described by `typeLayout`, at the given `offset`.
        void _addDescriptorRangesAsValue(slang::TypeLayoutReflection* typeLayout, BindingOffset const& offset);

        /// Add the descriptor ranges implied by a `ConstantBuffer<X>` where `X` is
        /// described by `elementTypeLayout`.
        ///
        /// The `containerOffset` and `elementOffset` are the binding offsets that
        /// should apply to the buffer itself and the contents of the buffer, respectively.
        ///
        void _addDescriptorRangesAsConstantBuffer(
            slang::TypeLayoutReflection* elementTypeLayout,
            BindingOffset const& containerOffset,
            BindingOffset const& elementOffset
        );

        /// Add binding ranges to this shader object layout, as implied by the given
        /// `typeLayout`
        void addBindingRanges(slang::TypeLayoutReflection* typeLayout);

        Result setElementTypeLayout(slang::TypeLayoutReflection* typeLayout);

        Result build(ShaderObjectLayoutImpl** outLayout);
    };

    static Result createForElementType(
        DeviceImpl* device,
        slang::ISession* session,
        slang::TypeLayoutReflection* elementType,
        ShaderObjectLayoutImpl** outLayout
    );

    ~ShaderObjectLayoutImpl();

    /// Get the number of descriptor sets that are allocated for this object itself
    /// (if it needed to be bound as a parameter block).
    ///
    uint32_t getOwnDescriptorSetCount() { return uint32_t(m_descriptorSetInfos.size()); }

    /// Get information about the descriptor sets that would be allocated to
    /// represent this object itself as a parameter block.
    ///
    std::vector<DescriptorSetInfo> const& getOwnDescriptorSets() { return m_descriptorSetInfos; }

    /// Get the number of descriptor sets that would need to be allocated and bound
    /// to represent the children of this object if it were bound as a parameter
    /// block.
    ///
    /// To a first approximation, this is the number of (transitive) children
    /// that are declared as `ParameterBlock<X>`.
    ///
    uint32_t getChildDescriptorSetCount() { return m_childDescriptorSetCount; }

    /// Get the total number of descriptor sets that would need to be allocated and bound
    /// to represent this object and its children (transitively) as a parameter block.
    ///
    uint32_t getTotalDescriptorSetCount() { return getOwnDescriptorSetCount() + getChildDescriptorSetCount(); }

    /// Get the total number of `binding`s required to represent this type and its
    /// (transitive) children.
    ///
    /// Note that this count does *not* include bindings that would be part of child
    /// parameter blocks, nor does it include the binding for an ordinary data buffer,
    /// if one is needed.
    ///
    uint32_t getTotalBindingCount() { return m_totalBindingCount; }

    uint32_t getTotalOrdinaryDataSize() const { return m_totalOrdinaryDataSize; }

    std::vector<BindingRangeInfo> const& getBindingRanges() { return m_bindingRanges; }

    Index getBindingRangeCount() { return m_bindingRanges.size(); }

    BindingRangeInfo const& getBindingRange(Index index) { return m_bindingRanges[index]; }

    Index getResourceCount() { return m_resourceCount; }
    Index getSamplerCount() { return m_samplerCount; }
    Index getSubObjectCount() { return m_subObjectCount; }

    SubObjectRangeInfo const& getSubObjectRange(Index index) { return m_subObjectRanges[index]; }
    std::vector<SubObjectRangeInfo> const& getSubObjectRanges() { return m_subObjectRanges; }

    DeviceImpl* getDevice();

    slang::TypeReflection* getType() { return m_elementTypeLayout->getType(); }

protected:
    Result _init(Builder const* builder);

    std::vector<DescriptorSetInfo> m_descriptorSetInfos;
    std::vector<BindingRangeInfo> m_bindingRanges;
    Index m_resourceCount = 0;
    Index m_samplerCount = 0;
    Index m_subObjectCount = 0;
    uint32_t m_childDescriptorSetCount = 0;
    uint32_t m_totalBindingCount = 0;
    uint32_t m_totalOrdinaryDataSize = 0;

    std::vector<SubObjectRangeInfo> m_subObjectRanges;
};

class EntryPointLayout : public ShaderObjectLayoutImpl
{
    typedef ShaderObjectLayoutImpl Super;

public:
    struct Builder : Super::Builder
    {
        Builder(DeviceImpl* device, slang::ISession* session)
            : Super::Builder(device, session)
        {
        }

        Result build(EntryPointLayout** outLayout);

        void addEntryPointParams(slang::EntryPointLayout* entryPointLayout);

        slang::EntryPointLayout* m_slangEntryPointLayout = nullptr;

        SlangStage m_shaderStageFlag;
    };

    Result _init(Builder const* builder);

    SlangStage getShaderStageFlag() const { return m_shaderStageFlag; }

    slang::EntryPointLayout* getSlangLayout() const { return m_slangEntryPointLayout; };

    slang::EntryPointLayout* m_slangEntryPointLayout;
    SlangStage m_shaderStageFlag;
};

class RootShaderObjectLayout : public ShaderObjectLayoutImpl
{
    typedef ShaderObjectLayoutImpl Super;

public:
    ~RootShaderObjectLayout();

    /// Information stored for each entry point of the program
    struct EntryPointInfo
    {
        /// Layout of the entry point
        RefPtr<EntryPointLayout> layout;

        /// Offset for binding the entry point, relative to the start of the program
        BindingOffset offset;
    };

    struct Builder : Super::Builder
    {
        Builder(DeviceImpl* device, slang::IComponentType* program, slang::ProgramLayout* programLayout)
            : Super::Builder(device, program->getSession())
            , m_program(program)
            , m_programLayout(programLayout)
        {
        }

        Result build(RootShaderObjectLayout** outLayout);

        void addGlobalParams(slang::VariableLayoutReflection* globalsLayout);

        void addEntryPoint(EntryPointLayout* entryPointLayout);

        slang::IComponentType* m_program;
        slang::ProgramLayout* m_programLayout;
        std::vector<EntryPointInfo> m_entryPoints;

        /// Offset to apply to "pending" data from this object, sub-objects, and entry points
        SimpleBindingOffset m_pendingDataOffset;
    };

    Index findEntryPointIndex(SlangStage stage);

    EntryPointInfo const& getEntryPoint(Index index) { return m_entryPoints[index]; }

    std::vector<EntryPointInfo> const& getEntryPoints() const { return m_entryPoints; }

    static Result create(
        DeviceImpl* device,
        slang::IComponentType* program,
        slang::ProgramLayout* programLayout,
        RootShaderObjectLayout** outLayout
    );

    SimpleBindingOffset const& getPendingDataOffset() const { return m_pendingDataOffset; }

    slang::IComponentType* getSlangProgram() const { return m_program; }
    slang::ProgramLayout* getSlangProgramLayout() const { return m_programLayout; }

protected:
    Result _init(Builder const* builder);

    /// Add all the descriptor sets implied by this root object and sub-objects
    Result addAllDescriptorSets();

    /// Recurisvely add descriptor sets defined by `layout` and sub-objects
    Result addAllDescriptorSetsRec(ShaderObjectLayoutImpl* layout);

    /// Recurisvely add descriptor sets defined by sub-objects of `layout`
    Result addChildDescriptorSetsRec(ShaderObjectLayoutImpl* layout);

public:
    ComPtr<slang::IComponentType> m_program;
    slang::ProgramLayout* m_programLayout = nullptr;
    std::vector<EntryPointInfo> m_entryPoints;
    WGPUPipelineLayout m_pipelineLayout = nullptr;
    static_vector<WGPUBindGroupLayout, kMaxDescriptorSets> m_bindGroupLayouts;

    SimpleBindingOffset m_pendingDataOffset;
    DeviceImpl* m_device = nullptr;
};

} // namespace rhi::wgpu
