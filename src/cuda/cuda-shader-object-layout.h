#pragma once

#include "cuda-base.h"

namespace rhi::cuda {

struct BindingOffset
{
    uint32_t uniformOffset = 0;
};

class ShaderObjectLayoutImpl : public ShaderObjectLayout
{
    using Super = ShaderObjectLayout;

public:
    struct BindingRangeInfo : Super::BindingRangeInfo
    {
        // TODO: The `uniformOffset` field should be removed,
        // since it cannot be supported by the Slang reflection
        // API once we fix some design issues.
        //
        // It is only being used today for pre-allocation of sub-objects
        // for constant buffers and parameter blocks (which should be
        // deprecated/removed anyway).
        //
        // Note: We would need to bring this field back, plus
        // a lot of other complexity, if we ever want to support
        // setting of resources/buffers directly by a binding
        // range index and array index.
        //
        uint32_t uniformOffset; // Uniform offset for a resource typed field.
    };

    struct SubObjectRangeInfo : Super::SubObjectRangeInfo
    {
        RefPtr<ShaderObjectLayoutImpl> layout;
    };

    std::vector<BindingRangeInfo> m_bindingRanges;
    std::vector<SubObjectRangeInfo> m_subObjectRanges;

    uint32_t m_slotCount = 0;
    uint32_t m_subObjectCount = 0;

    ShaderObjectLayoutImpl(Device* device, slang::ISession* session, slang::TypeLayoutReflection* layout);

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
};

class RootShaderObjectLayoutImpl : public ShaderObjectLayoutImpl
{
    using Super = ShaderObjectLayoutImpl;

public:
    struct EntryPointInfo : Super::EntryPointInfo
    {
        RefPtr<ShaderObjectLayoutImpl> layout;
    };

    slang::ProgramLayout* m_programLayout = nullptr;
    std::vector<EntryPointInfo> m_entryPoints;

    RootShaderObjectLayoutImpl(Device* device, slang::ProgramLayout* programLayout);

    int getKernelIndex(std::string_view kernelName);
    void getKernelThreadGroupSize(int kernelIndex, uint32_t* threadGroupSizes);

    // ShaderObjectLayout interface
    virtual uint32_t getEntryPointCount() const override { return m_entryPoints.size(); }
    virtual const EntryPointInfo& getEntryPoint(uint32_t index) const override { return m_entryPoints[index]; }
    virtual ShaderObjectLayout* getEntryPointLayout(uint32_t index) const override
    {
        return m_entryPoints[index].layout;
    }
};

} // namespace rhi::cuda
