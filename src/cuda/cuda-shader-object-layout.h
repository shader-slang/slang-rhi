#pragma once

#include "cuda-base.h"

namespace rhi::cuda {

struct BindingRangeInfo
{
    slang::BindingType bindingType;
    Index count;
    Index baseIndex; // Flat index for sub-objects
    Index subObjectIndex;

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
    Index uniformOffset; // Uniform offset for a resource typed field.

    bool isSpecializable;
};

struct SubObjectRangeInfo
{
    RefPtr<ShaderObjectLayoutImpl> layout;
    Index bindingRangeIndex;
};

class ShaderObjectLayoutImpl : public ShaderObjectLayout
{
public:
    std::vector<SubObjectRangeInfo> subObjectRanges;
    std::vector<BindingRangeInfo> m_bindingRanges;

    Index m_subObjectCount = 0;
    Index m_resourceCount = 0;

    ShaderObjectLayoutImpl(Device* device, slang::ISession* session, slang::TypeLayoutReflection* layout);

    Index getResourceCount() const { return m_resourceCount; }
    Index getSubObjectCount() const { return m_subObjectCount; }
    std::vector<SubObjectRangeInfo>& getSubObjectRanges() { return subObjectRanges; }
    const BindingRangeInfo& getBindingRange(Index index) { return m_bindingRanges[index]; }
    Index getBindingRangeCount() const { return m_bindingRanges.size(); }
};

class RootShaderObjectLayoutImpl : public ShaderObjectLayoutImpl
{
public:
    slang::ProgramLayout* programLayout = nullptr;
    std::vector<RefPtr<ShaderObjectLayoutImpl>> entryPointLayouts;
    RootShaderObjectLayoutImpl(Device* device, slang::ProgramLayout* inProgramLayout);

    int getKernelIndex(std::string_view kernelName);

    void getKernelThreadGroupSize(int kernelIndex, SlangUInt* threadGroupSizes);
};

} // namespace rhi::cuda
