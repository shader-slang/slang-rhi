#pragma once

#include "metal-base.h"

#include <vector>

namespace rhi::metal {

/// A "simple" binding offset that records an offset in buffer/texture/sampler slots
struct BindingOffset
{
    uint32_t buffer = 0;
    uint32_t texture = 0;
    uint32_t sampler = 0;

    /// Create a default (zero) offset
    BindingOffset() = default;

    /// Create an offset based on offset information in the given Slang `varLayout`
    BindingOffset(slang::VariableLayoutReflection* varLayout)
    {
        if (varLayout)
        {
            buffer = (uint32_t)varLayout->getOffset(SLANG_PARAMETER_CATEGORY_METAL_BUFFER);
            texture = (uint32_t)varLayout->getOffset(SLANG_PARAMETER_CATEGORY_METAL_TEXTURE);
            sampler = (uint32_t)varLayout->getOffset(SLANG_PARAMETER_CATEGORY_METAL_SAMPLER);
        }
    }

    /// Create an offset based on size/stride information in the given Slang `typeLayout`
    BindingOffset(slang::TypeLayoutReflection* typeLayout)
    {
        if (typeLayout)
        {
            buffer = (uint32_t)typeLayout->getSize(SLANG_PARAMETER_CATEGORY_METAL_BUFFER);
            texture = (uint32_t)typeLayout->getSize(SLANG_PARAMETER_CATEGORY_METAL_TEXTURE);
            sampler = (uint32_t)typeLayout->getSize(SLANG_PARAMETER_CATEGORY_METAL_SAMPLER);
        }
    }

    /// Add any values in the given `offset`
    void operator+=(const BindingOffset& offset)
    {
        buffer += offset.buffer;
        texture += offset.texture;
        sampler += offset.sampler;
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
    // ranges in a form that is usable for the Metal API:

    /// Information about a logical binding range as reported by Slang reflection
    struct BindingRangeInfo : Super::BindingRangeInfo
    {
        /// The offset of this binding range from the start of the sub-object.
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

        /// The stride for "pending" ordinary data related to this range
        uint32_t pendingOrdinaryData = 0;
    };

    /// Information about a logical binding range as reported by Slang reflection
    struct SubObjectRangeInfo : Super::SubObjectRangeInfo
    {
        /// The layout expected for objects bound to this range (if known)
        RefPtr<ShaderObjectLayoutImpl> layout;

        /// The offset to use when binding the first object in this range
        SubObjectRangeOffset offset;

        /// Stride between consecutive objects in this range
        SubObjectRangeStride stride;
    };

    uint32_t m_slotCount = 0;
    uint32_t m_subObjectCount = 0;
    uint32_t m_totalOrdinaryDataSize = 0;
    BindingOffset m_resourceCount;
    BindingOffset m_totalResourceCount;

    std::vector<BindingRangeInfo> m_bindingRanges;
    std::vector<SubObjectRangeInfo> m_subObjectRanges;

    // The type layout to use when the shader object is bind as a parameter block.
    slang::TypeLayoutReflection* m_parameterBlockTypeLayout = nullptr;

    static Result createForElementType(
        Device* device,
        slang::ISession* session,
        slang::TypeLayoutReflection* elementType,
        ShaderObjectLayoutImpl** outLayout
    );

    uint32_t getTotalBufferCount() { return m_totalResourceCount.buffer; }
    uint32_t getTotalTextureCount() { return m_totalResourceCount.texture; }
    uint32_t getTotalSamplerCount() { return m_totalResourceCount.sampler; }

    Device* getDevice() { return m_device; }

    slang::TypeReflection* getType() { return m_elementTypeLayout->getType(); }

    uint32_t getTotalOrdinaryDataSize() const { return m_totalOrdinaryDataSize; }

    slang::TypeLayoutReflection* getParameterBlockTypeLayout();

    // ShaderObjectLayout interface
    virtual uint32_t getSlotCount() const override { return m_slotCount; }
    virtual uint32_t getSubObjectCount() const override { return m_subObjectCount; }

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

        uint32_t m_slotCount = 0;
        uint32_t m_subObjectCount = 0;
        BindingOffset m_resourceCount;
        BindingOffset m_totalResourceCount;
        uint32_t m_totalOrdinaryDataSize = 0;

        /// The container type of this shader object. When `m_containerType` is
        /// `StructuredBuffer` or `Array`, this shader object represents a collection
        /// instead of a single object.
        ShaderObjectContainerType m_containerType = ShaderObjectContainerType::None;

        Result setElementTypeLayout(slang::TypeLayoutReflection* typeLayout);
        Result build(ShaderObjectLayoutImpl** outLayout);
    };

    Result _init(const Builder* builder);
};

class RootShaderObjectLayoutImpl : public ShaderObjectLayoutImpl
{
    typedef ShaderObjectLayoutImpl Super;

public:
    struct EntryPointInfo : Super::EntryPointInfo
    {
        RefPtr<ShaderObjectLayoutImpl> layout;

        /// The offset for this entry point's parameters, relative to the starting offset for the program
        BindingOffset offset;
    };

    ComPtr<slang::IComponentType> m_program;
    slang::ProgramLayout* m_programLayout = nullptr;

    std::vector<EntryPointInfo> m_entryPoints;

    EntryPointInfo& getEntryPoint(uint32_t index) { return m_entryPoints[index]; }

    std::vector<EntryPointInfo>& getEntryPoints() { return m_entryPoints; }

    static Result create(
        Device* device,
        slang::IComponentType* program,
        slang::ProgramLayout* programLayout,
        RootShaderObjectLayoutImpl** outLayout
    );

    slang::IComponentType* getSlangProgram() const { return m_program; }
    slang::ProgramLayout* getSlangProgramLayout() const { return m_programLayout; }

    // ShaderObjectLayout interface
    virtual uint32_t getEntryPointCount() const override { return (uint32_t)m_entryPoints.size(); }
    virtual const EntryPointInfo& getEntryPoint(uint32_t index) const override { return m_entryPoints[index]; }
    virtual ShaderObjectLayout* getEntryPointLayout(uint32_t index) const override
    {
        return m_entryPoints[index].layout;
    }

protected:
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
    };

    Result _init(const Builder* builder);
};

} // namespace rhi::metal
