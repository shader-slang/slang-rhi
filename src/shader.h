#pragma once

#include <slang-rhi/synthetic-bindings.h>

#include "core/common.h"
#include "core/timer.h"

#include "rhi-shared-fwd.h"
#include "device-child.h"

#include <string>
#include <memory>
#include <mutex>
#include <vector>

namespace rhi {

struct ShaderModuleDesc
{
    SlangStage stage = SLANG_STAGE_NONE;
    const char* entryPointName = nullptr;
};

struct EntryPointCompilationStats
{
    TimePoint startTime = {};
    TimePoint endTime = {};
    double totalTime = 0.0;
    double downstreamTime = 0.0;
    bool isCached = false;
    size_t cacheSize = 0;
};

struct CompiledEntryPoint
{
    ComPtr<slang::IComponentType> componentType;
    uint32_t entryPointIndex = 0;
    uint32_t targetIndex = 0;
    SlangStage stage = SLANG_STAGE_NONE;
    std::string name;
    ComPtr<ISlangBlob> cacheKey;
    // Backend-consumable artifact passed to createShaderModule. May include
    // backend metadata in addition to the generated shader code.
    ComPtr<ISlangBlob> code;
    ComPtr<ISlangBlob> diagnostics;
    EntryPointCompilationStats stats;
    Result result = SLANG_OK;
};

class SyntheticResourceBindingState;

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

    bool m_compiledShaders = false;

    /// Protects compilation state and backend shader modules.
    std::mutex m_compileMutex;

    // Optional state for compiler-synthesized resources. Null for ordinary
    // programs so the feature has no per-program vector/string storage cost.
    std::unique_ptr<SyntheticResourceBindingState> m_syntheticResources;

    ShaderProgram(Device* device, const ShaderProgramDesc& desc);
    virtual ~ShaderProgram() override;

    Result init();

    bool isMeshShaderProgram() const;

    Result compileShaders(Device* device);

    /// Must be called while holding m_compileMutex. Performs only front-end/reflection work.
    Result prepareEntryPointCompilation(Device* device, std::vector<CompiledEntryPoint>& outEntryPoints);

    /// Performs the concurrency-safe backend code generation operation for one prepared entry point.
    virtual Result compileEntryPoint(Device* device, CompiledEntryPoint& entryPoint, bool measureCompilerTime);

    /// Reports diagnostics and timing on the caller thread.
    void reportEntryPointCompilation(Device* device, const CompiledEntryPoint& entryPoint);

    /// Must be called while holding m_compileMutex after every entry point compiled successfully.
    Result installCompiledEntryPoints(std::span<const CompiledEntryPoint> entryPoints);

    virtual Result createShaderModule(const ShaderModuleDesc& desc, ComPtr<ISlangBlob> kernelCode);

    SyntheticResourceBindingState* getSyntheticResourceBindingState() { return m_syntheticResources.get(); }
    const SyntheticResourceBindingState* getSyntheticResourceBindingState() const { return m_syntheticResources.get(); }

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
        size_t cacheSize,
        ISlangBlob* cacheKey
    );

    void reportCreatePipeline(
        ShaderProgram* program,
        PipelineType pipelineType,
        TimePoint startTime,
        TimePoint endTime,
        bool isCached,
        size_t cacheSize,
        ISlangBlob* cacheKey
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
