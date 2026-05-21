#pragma once

#include <slang-rhi/synthetic-bindings.h>

#include "core/common.h"
#include "core/short_vector.h"
#include "core/timer.h"

#include "rhi-shared-fwd.h"
#include "device-child.h"

#include <string>
#include <memory>
#include <unordered_map>
#include <vector>

namespace rhi {

struct SyntheticResourceBindingRecord;
class SyntheticResourceBindingState;

struct SpecializationKey
{
    short_vector<ShaderComponentID> componentIDs;

    SpecializationKey(const ExtendedShaderObjectTypeList& args);

    bool operator==(const SpecializationKey& rhs) const { return componentIDs == rhs.componentIDs; }

    struct Hasher
    {
        std::size_t operator()(const SpecializationKey& key) const
        {
            size_t hash = 0;
            for (auto& arg : key.componentIDs)
                hash_combine(hash, arg);
            return hash;
        }
    };
};

using ShaderProgramID = uint64_t;

class ShaderProgram : public IShaderProgram, public ISyntheticShaderProgram, public DeviceChild
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL
    void* getInterface(const Guid& guid);

    ShaderProgramDesc m_desc;
    StructHolder m_descHolder;

    ShaderProgramID m_id;

    ComPtr<slang::IComponentType> slangGlobalScope;
    std::vector<ComPtr<slang::IComponentType>> slangEntryPoints;

    // Linked program when linkingStyle is SingleProgram, or the original global scope
    // when linking style is SeparateEntryPointCompilation.
    ComPtr<slang::IComponentType> linkedProgram;

    // Linked program for each entry point when linkingStyle is SeparateEntryPointCompilation.
    std::vector<ComPtr<slang::IComponentType>> linkedEntryPoints;

    bool m_isSpecializable = false;

    bool m_compiledShaders = false;

    std::unordered_map<SpecializationKey, RefPtr<ShaderProgram>, SpecializationKey::Hasher> m_specializedPrograms;

    // Optional state for compiler-synthesized resources. Null for ordinary
    // programs so the feature has no per-program vector/string storage cost.
    std::unique_ptr<SyntheticResourceBindingState> m_syntheticResources;

    ShaderProgram(Device* device, const ShaderProgramDesc& desc);
    virtual ~ShaderProgram() override;

    Result init();

    bool isSpecializable() const { return m_isSpecializable; }
    bool isMeshShaderProgram() const;

    Result compileShaders(Device* device);

    virtual Result createShaderModule(slang::EntryPointReflection* entryPointInfo, ComPtr<ISlangBlob> kernelCode);

    bool hasSyntheticResourceInputs() const;
    const std::vector<SyntheticResourceBindingRecord>& getSyntheticResourceInputs() const;
    Result setResolvedSyntheticBindingLocations(const std::vector<SyntheticBindingLocation>& locations);

    // IShaderProgram interface
    virtual SLANG_NO_THROW const ShaderProgramDesc& SLANG_MCALL getDesc() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getCompilationReport(ISlangBlob** outReportBlob) override;
    virtual SLANG_NO_THROW slang::TypeReflection* SLANG_MCALL findTypeByName(const char* name) override;

    // ISyntheticShaderProgram interface
    virtual SLANG_NO_THROW uint32_t SLANG_MCALL getSyntheticBindingCount() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getSyntheticBindingLocation(
        uint32_t index,
        SyntheticBindingLocation* outLocation
    ) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL findSyntheticBindingLocationByID(
        uint32_t syntheticResourceID,
        SyntheticBindingLocation* outLocation
    ) override;

    virtual ShaderObjectLayout* getRootShaderObjectLayout() = 0;

private:
    uint32_t _getEntryPointCount() const;
    Result _initSyntheticResourceDescs();

    bool _isSpecializable()
    {
        if (slangGlobalScope->getSpecializationParamCount() != 0)
        {
            return true;
        }
        for (auto& entryPoint : slangEntryPoints)
        {
            if (entryPoint->getSpecializationParamCount() != 0)
            {
                return true;
            }
        }
        return false;
    }
};

class ShaderCompilationReporter : public RefObject
{
public:
    using PipelineType = CompilationReport::PipelineType;
    using EntryPointReport = CompilationReport::EntryPointReport;
    using PipelineReport = CompilationReport::PipelineReport;

    ShaderCompilationReporter(Device* device);

    void registerProgram(ShaderProgram* program);
    void unregisterProgram(ShaderProgram* program);

    void reportCompileEntryPoint(
        ShaderProgram* program,
        const char* entryPointName,
        TimePoint startTime,
        TimePoint endTime,
        double totalTime,
        double downstreamTime,
        bool isCached,
        size_t cacheSize
    );

    void reportCreatePipeline(
        ShaderProgram* program,
        PipelineType pipelineType,
        TimePoint startTime,
        TimePoint endTime,
        bool isCached,
        size_t cacheSize
    );

    Result getCompilationReport(ShaderProgram* program, ISlangBlob** outReportBlob);
    Result getCompilationReportList(ISlangBlob** outReportListBlob);

private:
    struct ProgramReport
    {
        bool alive = false;
        std::string label;
        std::vector<EntryPointReport> entryPointReports;
        std::vector<PipelineReport> pipelineReports;
    };

    Device* m_device;

    bool m_printReports = false;
    bool m_recordReports = false;

    /// Mutex to protect access to m_programReports.
    std::mutex m_mutex;
    /// Maps ShaderProgramID to ProgramReport.
    std::vector<ProgramReport> m_programReports;

    void writeCompilationReport(
        CompilationReport* dst,
        EntryPointReport* dstEntryPoints,
        PipelineReport* dstPipelines,
        const ProgramReport& src
    );
};

} // namespace rhi
