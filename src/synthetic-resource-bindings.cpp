#include "synthetic-resource-bindings.h"

namespace rhi {

namespace {

struct DescStructHeaderView
{
    StructType type;
    const void* next;
};

} // namespace

Result SyntheticResourceBindingState::init(const ShaderProgramDesc& desc, uint32_t entryPointCount)
{
    m_descHolder.reset();
    m_resourcesDesc = {};
    m_inputs.clear();
    m_locations.clear();

    for (const auto* header = static_cast<const DescStructHeaderView*>(desc.next); header;
         header = static_cast<const DescStructHeaderView*>(header->next))
    {
        switch (header->type)
        {
        case StructType::ShaderProgramSyntheticResourcesDesc:
        {
            const auto* syntheticDesc = reinterpret_cast<const ShaderProgramSyntheticResourcesDesc*>(header);
            if (syntheticDesc->resourceCount && !syntheticDesc->resources)
                return SLANG_E_INVALID_ARG;

            m_inputs.reserve(m_inputs.size() + syntheticDesc->resourceCount);
            for (uint32_t i = 0; i < syntheticDesc->resourceCount; ++i)
            {
                const auto& src = syntheticDesc->resources[i];
                SyntheticResourceBindingRecord record;
                record.id = src.id;
                record.bindingType = src.bindingType;
                record.arraySize = src.arraySize;
                record.scope = src.scope;
                record.access = src.access;
                record.entryPointIndex = src.entryPointIndex;
                record.space = src.space;
                record.binding = src.binding;
                record.uniformOffset = src.uniformOffset;
                record.uniformStride = src.uniformStride;
                if (src.debugName)
                    record.debugName = src.debugName;

                SLANG_RETURN_ON_FAIL(_validateRecord(record, entryPointCount));

                for (const auto& existing : m_inputs)
                {
                    if (existing.id == record.id)
                        return SLANG_E_INVALID_ARG;
                }

                m_inputs.push_back(record);
            }
            break;
        }
        default:
            return SLANG_E_INVALID_ARG;
        }
    }

    if (!m_inputs.empty())
        SLANG_RETURN_ON_FAIL(_rebuildPublicDesc());

    return SLANG_OK;
}

Result SyntheticResourceBindingState::_validateRecord(
    const SyntheticResourceBindingRecord& record,
    uint32_t entryPointCount
) const
{
    if (record.id == 0)
        return SLANG_E_INVALID_ARG;
    if (record.bindingType == slang::BindingType::Unknown)
        return SLANG_E_INVALID_ARG;
    if (record.arraySize == 0)
        return SLANG_E_INVALID_ARG;
    if (record.space < -1 || record.binding < -1 || record.uniformOffset < -1)
        return SLANG_E_INVALID_ARG;
    if (record.uniformStride < 0)
        return SLANG_E_INVALID_ARG;
    if (record.scope == SyntheticResourceScope::EntryPoint)
    {
        if (record.entryPointIndex < 0)
            return SLANG_E_INVALID_ARG;
        if ((uint32_t)record.entryPointIndex >= entryPointCount)
            return SLANG_E_INVALID_ARG;
    }
    else
    {
        if (record.entryPointIndex != -1)
            return SLANG_E_INVALID_ARG;
    }
    if (record.uniformOffset == -1 && record.uniformStride != 0)
        return SLANG_E_INVALID_ARG;
    return SLANG_OK;
}

Result SyntheticResourceBindingState::_rebuildPublicDesc()
{
    std::vector<SyntheticResourceBindingDesc> copiedResources(m_inputs.size());
    for (size_t i = 0; i < m_inputs.size(); ++i)
    {
        auto& dst = copiedResources[i];
        const auto& src = m_inputs[i];
        dst.id = src.id;
        dst.bindingType = src.bindingType;
        dst.arraySize = src.arraySize;
        dst.scope = src.scope;
        dst.access = src.access;
        dst.entryPointIndex = src.entryPointIndex;
        dst.space = src.space;
        dst.binding = src.binding;
        dst.uniformOffset = src.uniformOffset;
        dst.uniformStride = src.uniformStride;
        dst.debugName = src.debugName.empty() ? nullptr : src.debugName.c_str();
    }

    SyntheticResourceBindingDesc* resources = copiedResources.data();
    m_descHolder.holdList(resources, copiedResources.size());
    for (size_t i = 0; i < copiedResources.size(); ++i)
    {
        m_descHolder.holdString(resources[i].debugName);
    }

    m_resourcesDesc = {};
    m_resourcesDesc.resources = resources;
    m_resourcesDesc.resourceCount = (uint32_t)copiedResources.size();
    return SLANG_OK;
}

Result SyntheticResourceBindingState::setResolvedLocations(const std::vector<SyntheticBindingLocation>& locations)
{
    m_locations.clear();
    for (const auto& location : locations)
    {
        SLANG_RETURN_ON_FAIL(_addResolvedLocation(location));
    }
    return SLANG_OK;
}

Result SyntheticResourceBindingState::_addResolvedLocation(const SyntheticBindingLocation& location)
{
    SyntheticBindingLocation ownedLocation = location;
    ownedLocation.structSize = sizeof(SyntheticBindingLocation);
    m_descHolder.holdString(ownedLocation.debugName);

    for (auto& existing : m_locations)
    {
        if (existing.syntheticResourceID == location.syntheticResourceID)
        {
            existing = ownedLocation;
            return SLANG_OK;
        }
    }
    m_locations.push_back(ownedLocation);
    return SLANG_OK;
}

Result SyntheticResourceBindingState::getLocation(uint32_t index, SyntheticBindingLocation* outLocation) const
{
    if (!outLocation)
        return SLANG_E_INVALID_ARG;
    if (outLocation->structSize < sizeof(SyntheticBindingLocation))
        return SLANG_E_INVALID_ARG;
    if (index >= m_locations.size())
        return SLANG_E_INVALID_ARG;

    *outLocation = m_locations[index];
    return SLANG_OK;
}

Result SyntheticResourceBindingState::findLocationByID(
    uint32_t syntheticResourceID,
    SyntheticBindingLocation* outLocation
) const
{
    if (!outLocation)
        return SLANG_E_INVALID_ARG;
    if (outLocation->structSize < sizeof(SyntheticBindingLocation))
        return SLANG_E_INVALID_ARG;

    for (const auto& location : m_locations)
    {
        if (location.syntheticResourceID == syntheticResourceID)
        {
            *outLocation = location;
            return SLANG_OK;
        }
    }
    return SLANG_E_INVALID_ARG;
}

} // namespace rhi
