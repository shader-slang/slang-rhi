#pragma once

#include <slang-rhi/synthetic-bindings.h>

#include "core/struct-holder.h"

#include <string>
#include <vector>

namespace rhi {

struct SyntheticResourceBindingRecord
{
    uint32_t id = 0;
    slang::BindingType bindingType = slang::BindingType::Unknown;
    uint32_t arraySize = 1;
    SyntheticResourceScope scope = SyntheticResourceScope::Global;
    SyntheticResourceAccess access = SyntheticResourceAccess::Read;
    int32_t entryPointIndex = -1;
    int32_t space = -1;
    int32_t binding = -1;
    int32_t uniformOffset = -1;
    int32_t uniformStride = 0;
    std::string debugName;
};

class SyntheticResourceBindingState
{
public:
    static const ShaderProgramSyntheticResourcesDesc* findDesc(const ShaderProgramDesc& desc);

    Result init(const ShaderProgramDesc& desc, uint32_t entryPointCount);

    bool hasResources() const { return !m_inputs.empty(); }

    const std::vector<SyntheticResourceBindingRecord>& getInputs() const { return m_inputs; }
    const ShaderProgramSyntheticResourcesDesc* getDesc() const { return hasResources() ? &m_resourcesDesc : nullptr; }

    Result setResolvedLocations(const std::vector<SyntheticBindingLocation>& locations);

    uint32_t getLocationCount() const { return (uint32_t)m_locations.size(); }
    Result getLocation(uint32_t index, SyntheticBindingLocation* outLocation) const;
    Result findLocationByID(uint32_t syntheticResourceID, SyntheticBindingLocation* outLocation) const;

private:
    Result _validateRecord(const SyntheticResourceBindingRecord& record, uint32_t entryPointCount) const;
    Result _addResolvedLocation(const SyntheticBindingLocation& location);
    Result _rebuildPublicDesc();

    StructHolder m_descHolder;
    ShaderProgramSyntheticResourcesDesc m_resourcesDesc;
    std::vector<SyntheticResourceBindingRecord> m_inputs;
    std::vector<SyntheticBindingLocation> m_locations;
};

} // namespace rhi
