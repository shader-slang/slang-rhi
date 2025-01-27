#pragma once

#include "cpu-base.h"

namespace rhi::cpu {

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

    uint32_t m_slotCount;
    uint32_t m_subObjectCount;
    std::vector<BindingRangeInfo> m_bindingRanges;
    std::vector<SubObjectRangeInfo> m_subObjectRanges;

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

class EntryPointLayoutImpl : public ShaderObjectLayoutImpl
{
private:
    slang::EntryPointLayout* m_entryPointLayout = nullptr;

public:
    EntryPointLayoutImpl(Device* device, slang::ISession* session, slang::EntryPointLayout* entryPointLayout)
        : ShaderObjectLayoutImpl(device, session, entryPointLayout->getTypeLayout())
        , m_entryPointLayout(entryPointLayout)
    {
    }

    const char* getEntryPointName() { return m_entryPointLayout->getName(); }
};

class RootShaderObjectLayoutImpl : public ShaderObjectLayoutImpl
{
    using Super = ShaderObjectLayoutImpl;

public:
    struct EntryPointInfo : Super::EntryPointInfo
    {
        RefPtr<EntryPointLayoutImpl> layout;
    };

    slang::ProgramLayout* m_programLayout = nullptr;
    std::vector<EntryPointInfo> m_entryPoints;

    RootShaderObjectLayoutImpl(Device* device, slang::ISession* session, slang::ProgramLayout* programLayout);

    int getKernelIndex(std::string_view kernelName);
    void getKernelThreadGroupSize(int kernelIndex, uint32_t* threadGroupSizes);

    // ShaderObjectLayoutImpl interface
    virtual uint32_t getEntryPointCount() const { return m_entryPoints.size(); }
    virtual const EntryPointInfo& getEntryPoint(uint32_t index) const { return m_entryPoints[index]; }
    virtual ShaderObjectLayout* getEntryPointLayout(uint32_t index) const { return m_entryPoints[index].layout; }
};

} // namespace rhi::cpu
