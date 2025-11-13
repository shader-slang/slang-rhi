#include "cuda-shader-object-layout.h"

namespace rhi::cuda {

ShaderObjectLayoutImpl::ShaderObjectLayoutImpl(
    Device* device,
    slang::ISession* session,
    slang::TypeLayoutReflection* layout
)
{
    m_elementTypeLayout = _unwrapParameterGroups(layout, m_containerType);

    initBase(device, session, m_elementTypeLayout);

    // Compute the binding ranges that are used to store
    // the logical contents of the object in memory. These will relate
    // to the descriptor ranges in the various sets, but not always
    // in a one-to-one fashion.

    SlangInt bindingRangeCount = m_elementTypeLayout->getBindingRangeCount();
    for (SlangInt r = 0; r < bindingRangeCount; ++r)
    {
        slang::BindingType slangBindingType = m_elementTypeLayout->getBindingRangeType(r);
        SlangInt count = m_elementTypeLayout->getBindingRangeBindingCount(r);
        slang::TypeLayoutReflection* slangLeafTypeLayout = m_elementTypeLayout->getBindingRangeLeafTypeLayout(r);

        SlangInt descriptorSetIndex = m_elementTypeLayout->getBindingRangeDescriptorSetIndex(r);
        SlangInt rangeIndexInDescriptorSet = m_elementTypeLayout->getBindingRangeFirstDescriptorRangeIndex(r);

        // TODO: This logic assumes that for any binding range that might consume
        // multiple kinds of resources, the descriptor range for its uniform
        // usage will be the first one in the range.
        //
        // We need to decide whether that assumption is one we intend to support
        // applications making, or whether they should be forced to perform a
        // linear search over the descriptor ranges for a specific binding range.
        //
        auto uniformOffset = m_elementTypeLayout->getDescriptorSetDescriptorRangeIndexOffset(
            descriptorSetIndex,
            rangeIndexInDescriptorSet
        );

        uint32_t slotIndex = 0;
        uint32_t subObjectIndex = 0;

        switch (slangBindingType)
        {
        case slang::BindingType::ConstantBuffer:
        case slang::BindingType::ParameterBlock:
        case slang::BindingType::ExistentialValue:
            subObjectIndex = m_subObjectCount;
            m_subObjectCount += count;
            break;
        case slang::BindingType::RawBuffer:
        case slang::BindingType::MutableRawBuffer:
            if (slangLeafTypeLayout->getType()->getElementType() != nullptr)
            {
                // A structured buffer occupies both a resource slot and
                // a sub-object slot.
                subObjectIndex = m_subObjectCount;
                m_subObjectCount += count;
            }
            slotIndex = m_slotCount;
            m_slotCount += count;
            break;
        default:
            slotIndex = m_slotCount;
            m_slotCount += count;
            break;
        }

        BindingRangeInfo bindingRangeInfo;
        bindingRangeInfo.bindingType = slangBindingType;
        bindingRangeInfo.count = count;
        bindingRangeInfo.slotIndex = slotIndex;
        bindingRangeInfo.uniformOffset = uniformOffset;
        bindingRangeInfo.subObjectIndex = subObjectIndex;
        bindingRangeInfo.isSpecializable = m_elementTypeLayout->isBindingRangeSpecializable(r);
        m_bindingRanges.push_back(bindingRangeInfo);
    }

    SlangInt subObjectRangeCount = m_elementTypeLayout->getSubObjectRangeCount();
    for (SlangInt r = 0; r < subObjectRangeCount; ++r)
    {
        SlangInt bindingRangeIndex = m_elementTypeLayout->getSubObjectRangeBindingRangeIndex(r);
        auto slangBindingType = m_elementTypeLayout->getBindingRangeType(bindingRangeIndex);
        slang::TypeLayoutReflection* slangLeafTypeLayout =
            m_elementTypeLayout->getBindingRangeLeafTypeLayout(bindingRangeIndex);

        // A sub-object range can either represent a sub-object of a known
        // type, like a `ConstantBuffer<Foo>` or `ParameterBlock<Foo>`
        // (in which case we can pre-compute a layout to use, based on
        // the type `Foo`) *or* it can represent a sub-object of some
        // existential type (e.g., `IBar`) in which case we cannot
        // know the appropriate type/layout of sub-object to allocate.
        //
        RefPtr<ShaderObjectLayoutImpl> subObjectLayout;
        if (slangBindingType != slang::BindingType::ExistentialValue)
        {
            subObjectLayout = new ShaderObjectLayoutImpl(device, session, slangLeafTypeLayout->getElementTypeLayout());
        }

        SubObjectRangeInfo subObjectRange;
        subObjectRange.bindingRangeIndex = bindingRangeIndex;
        subObjectRange.layout = subObjectLayout;
        m_subObjectRanges.push_back(subObjectRange);
    }
}

// Compute the size of the entry point parameters passed to cuLaunchKernel.
// There is an issue in Slang where the entry point parameters seem to be treated as having C struct
// layout rules. While this matches how CUDA expects parameters to be layed out, CUDA expects *NO* padding
// at the end of the parameter buffer. Therefore we compute the size manually here.
inline size_t computeEntryPointParamsSize(slang::EntryPointReflection* entryPointReflection)
{
    size_t paramsSize = 0;
    for (unsigned int i = 0; i < entryPointReflection->getParameterCount(); ++i)
    {
        slang::VariableLayoutReflection* variableLayout = entryPointReflection->getParameterByIndex(i);
        size_t offset = variableLayout->getOffset();
        size_t size = variableLayout->getTypeLayout()->getSize();
        paramsSize = std::max(paramsSize, offset + size);
    }
    return paramsSize;
}

RootShaderObjectLayoutImpl::RootShaderObjectLayoutImpl(Device* device, slang::ProgramLayout* programLayout)
    : ShaderObjectLayoutImpl(device, programLayout->getSession(), programLayout->getGlobalParamsTypeLayout())
    , m_programLayout(programLayout)
{
    for (SlangUInt i = 0; i < programLayout->getEntryPointCount(); i++)
    {
        EntryPointInfo entryPointInfo;
        entryPointInfo.layout = new ShaderObjectLayoutImpl(
            device,
            programLayout->getSession(),
            programLayout->getEntryPointByIndex(i)->getTypeLayout()
        );
        entryPointInfo.paramsSize = computeEntryPointParamsSize(programLayout->getEntryPointByIndex(i));
        m_entryPoints.push_back(entryPointInfo);
    }
}

int RootShaderObjectLayoutImpl::getKernelIndex(std::string_view kernelName)
{
    for (SlangUInt i = 0; i < m_programLayout->getEntryPointCount(); i++)
    {
        auto entryPoint = m_programLayout->getEntryPointByIndex(i);
        if (kernelName == entryPoint->getName())
        {
            return i;
        }
    }
    return -1;
}

void RootShaderObjectLayoutImpl::getKernelThreadGroupSize(int kernelIndex, uint32_t* threadGroupSizes)
{
    auto entryPoint = m_programLayout->getEntryPointByIndex(kernelIndex);
    SlangUInt sizes[3];
    entryPoint->getComputeThreadGroupSize(3, sizes);
    threadGroupSizes[0] = (uint32_t)sizes[0];
    threadGroupSizes[1] = (uint32_t)sizes[1];
    threadGroupSizes[2] = (uint32_t)sizes[2];
}

} // namespace rhi::cuda
