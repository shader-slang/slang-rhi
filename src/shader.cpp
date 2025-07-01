#include "shader.h"

#include "rhi-shared.h"

namespace rhi {

// ----------------------------------------------------------------------------
// SpecializationKey
// ----------------------------------------------------------------------------

SpecializationKey::SpecializationKey(const ExtendedShaderObjectTypeList& args)
    : componentIDs(args.componentIDs)
{
}

// ----------------------------------------------------------------------------
// ShaderProgram
// ----------------------------------------------------------------------------

ShaderProgram::ShaderProgram(Device* device, const ShaderProgramDesc& desc)
    : DeviceChild(device)
    , m_desc(desc)
{
    m_descHolder.holdString(m_desc.label);
    m_descHolder.holdList(m_desc.slangEntryPoints, m_desc.slangEntryPointCount);

    m_id = device->m_nextShaderProgramID.fetch_add(1);

    if (m_device->m_shaderCompilationReporter)
    {
        m_device->m_shaderCompilationReporter->registerProgram(this);
    }
}

ShaderProgram::~ShaderProgram()
{
    if (m_device->m_shaderCompilationReporter)
    {
        m_device->m_shaderCompilationReporter->unregisterProgram(this);
    }
}

IShaderProgram* ShaderProgram::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IShaderProgram::getTypeGuid())
        return static_cast<IShaderProgram*>(this);
    return nullptr;
}

Result ShaderProgram::init()
{
    slangGlobalScope = m_desc.slangGlobalScope;
    for (uint32_t i = 0; i < m_desc.slangEntryPointCount; i++)
    {
        slangEntryPoints.push_back(ComPtr<slang::IComponentType>(m_desc.slangEntryPoints[i]));
    }

    auto session = m_desc.slangGlobalScope ? m_desc.slangGlobalScope->getSession() : nullptr;
    if (m_desc.linkingStyle == LinkingStyle::SingleProgram)
    {
        std::vector<slang::IComponentType*> components;
        if (m_desc.slangGlobalScope)
        {
            components.push_back(m_desc.slangGlobalScope);
        }
        for (uint32_t i = 0; i < m_desc.slangEntryPointCount; i++)
        {
            if (!session)
            {
                session = m_desc.slangEntryPoints[i]->getSession();
            }
            components.push_back(m_desc.slangEntryPoints[i]);
        }
        SLANG_RETURN_ON_FAIL(
            session->createCompositeComponentType(components.data(), components.size(), linkedProgram.writeRef())
        );
    }
    else
    {
        for (uint32_t i = 0; i < m_desc.slangEntryPointCount; i++)
        {
            if (m_desc.slangGlobalScope)
            {
                slang::IComponentType* entryPointComponents[2] = {m_desc.slangGlobalScope, m_desc.slangEntryPoints[i]};
                ComPtr<slang::IComponentType> linkedEntryPoint;
                SLANG_RETURN_ON_FAIL(
                    session->createCompositeComponentType(entryPointComponents, 2, linkedEntryPoint.writeRef())
                );
                linkedEntryPoints.push_back(linkedEntryPoint);
            }
            else
            {
                linkedEntryPoints.push_back(ComPtr<slang::IComponentType>(m_desc.slangEntryPoints[i]));
            }
        }
        linkedProgram = m_desc.slangGlobalScope;
    }

    m_isSpecializable = _isSpecializable();

    return SLANG_OK;
}

Result ShaderProgram::compileShaders(Device* device)
{
    if (m_compiledShaders)
        return SLANG_OK;

    if (device->getInfo().deviceType == DeviceType::CPU)
    {
        // CPU device does not need to compile shaders.
        m_compiledShaders = true;
        return SLANG_OK;
    }

    // For a fully specialized program, read and store its kernel code in `shaderProgram`.
    auto compileShader = [&](slang::EntryPointReflection* entryPointInfo,
                             slang::IComponentType* entryPointComponent,
                             uint32_t entryPointIndex)
    {
        ComPtr<ISlangBlob> kernelCode;
        ComPtr<ISlangBlob> diagnostics;
        auto compileResult = device->getEntryPointCodeFromShaderCache(
            this,
            entryPointComponent,
            entryPointInfo->getNameOverride(),
            entryPointIndex,
            0,
            kernelCode.writeRef(),
            diagnostics.writeRef()
        );
        if (diagnostics)
        {
            DebugMessageType msgType = DebugMessageType::Warning;
            if (compileResult != SLANG_OK)
                msgType = DebugMessageType::Error;
            device->handleMessage(msgType, DebugMessageSource::Slang, (char*)diagnostics->getBufferPointer());
        }
        SLANG_RETURN_ON_FAIL(compileResult);
        SLANG_RETURN_ON_FAIL(createShaderModule(entryPointInfo, kernelCode));
        return SLANG_OK;
    };

    if (linkedEntryPoints.size() == 0)
    {
        // If the user does not explicitly specify entry point components, find them from
        // `linkedEntryPoints`.
        auto programReflection = linkedProgram->getLayout();
        for (uint32_t i = 0; i < programReflection->getEntryPointCount(); i++)
        {
            SLANG_RETURN_ON_FAIL(compileShader(programReflection->getEntryPointByIndex(i), linkedProgram, i));
        }
    }
    else
    {
        // If the user specifies entry point components via the separated entry point array,
        // compile code from there.
        for (auto& entryPoint : linkedEntryPoints)
        {
            SLANG_RETURN_ON_FAIL(compileShader(entryPoint->getLayout()->getEntryPointByIndex(0), entryPoint, 0));
        }
    }

    m_compiledShaders = true;

    return SLANG_OK;
}

Result ShaderProgram::createShaderModule(slang::EntryPointReflection* entryPointInfo, ComPtr<ISlangBlob> kernelCode)
{
    SLANG_UNUSED(entryPointInfo);
    SLANG_UNUSED(kernelCode);
    return SLANG_OK;
}

bool ShaderProgram::isMeshShaderProgram() const
{
    // Similar to above, interrogate either explicity specified entry point
    // componenets or the ones in the linked program entry point array
    if (linkedEntryPoints.size())
    {
        for (const auto& e : linkedEntryPoints)
            if (e->getLayout()->getEntryPointByIndex(0)->getStage() == SLANG_STAGE_MESH)
                return true;
    }
    else
    {
        const auto programReflection = linkedProgram->getLayout();
        for (SlangUInt i = 0; i < programReflection->getEntryPointCount(); ++i)
            if (programReflection->getEntryPointByIndex(i)->getStage() == SLANG_STAGE_MESH)
                return true;
    }
    return false;
}

const ShaderProgramDesc& ShaderProgram::getDesc()
{
    return m_desc;
}

Result ShaderProgram::getCompilationReport(ISlangBlob** outReportBlob)
{
    if (m_device->m_shaderCompilationReporter)
    {
        return m_device->m_shaderCompilationReporter->getCompilationReport(this, outReportBlob);
    }
    return SLANG_E_NOT_AVAILABLE;
}

slang::TypeReflection* ShaderProgram::findTypeByName(const char* name)
{
    return linkedProgram->getLayout()->findTypeByName(name);
}

// ----------------------------------------------------------------------------
// ShaderCompilationReporter
// ----------------------------------------------------------------------------

ShaderCompilationReporter::ShaderCompilationReporter(Device* device)
    : m_device(device)
{
    m_printReports = true;
    m_recordReports = true;
}

void ShaderCompilationReporter::registerProgram(ShaderProgram* program)
{
    SLANG_RHI_ASSERT(program);

    const char* label = program->m_desc.label ? program->m_desc.label : "unnamed";

    if (m_printReports)
    {
        m_device->printInfo("Shader program %llu: Registered (label: \"%s\")", program->m_id, label);
    }

    if (m_recordReports)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_programReports.resize(max(size_t(program->m_id) + 1, m_programReports.size()));
        ProgramReport& programReport = m_programReports[program->m_id];
        programReport.alive = true;
        programReport.label = label;
    }
}

void ShaderCompilationReporter::unregisterProgram(ShaderProgram* program)
{
    SLANG_RHI_ASSERT(program);

    if (m_printReports)
    {
        m_device->printInfo("Shader program %llu: Unregistered", program->m_id);
    }

    if (m_recordReports)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        SLANG_RHI_ASSERT(program->m_id < m_programReports.size());
        ProgramReport& programReport = m_programReports[program->m_id];
        programReport.alive = false;
    }
}

void ShaderCompilationReporter::reportCompileEntryPoint(
    ShaderProgram* program,
    const char* entryPointName,
    TimePoint startTime,
    TimePoint endTime,
    double totalTime,
    double downstreamTime,
    bool isCached,
    size_t cacheSize
)
{
    SLANG_RHI_ASSERT(program);

    if (m_printReports)
    {
        m_device->printInfo(
            "Shader program %llu: Creating entry point \"%s\" took %.1f ms "
            "(compilation: %.1f ms, slang: %.1f ms, downstream: %.1f ms, cached: %s, cacheSize: %zd)",
            program->m_id,
            entryPointName,
            Timer::deltaMS(startTime, endTime),
            totalTime * 1e3,
            (totalTime - downstreamTime) * 1e3,
            downstreamTime * 1e3,
            isCached ? "yes" : "no",
            cacheSize
        );
    }

    if (m_recordReports)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        SLANG_RHI_ASSERT(program->m_id < m_programReports.size());
        ProgramReport& programReport = m_programReports[program->m_id];
        EntryPointReport& entryPointReport = programReport.entryPointReports.emplace_back();
        string::copy_safe(entryPointReport.name, sizeof(entryPointReport.name), entryPointName);
        entryPointReport.startTime = startTime;
        entryPointReport.endTime = endTime;
        entryPointReport.createTime = Timer::delta(startTime, endTime);
        entryPointReport.compileTime = totalTime;
        entryPointReport.compileSlangTime = totalTime - downstreamTime;
        entryPointReport.compileDownstreamTime = downstreamTime;
        entryPointReport.isCached = isCached;
        entryPointReport.cacheSize = cacheSize;
    }
}

void ShaderCompilationReporter::reportCreatePipeline(
    ShaderProgram* program,
    PipelineType pipelineType,
    TimePoint startTime,
    TimePoint endTime,
    bool isCached,
    size_t cacheSize
)
{
    SLANG_RHI_ASSERT(program);

    auto getPipelineTypeName = [](PipelineType type) -> const char*
    {
        switch (type)
        {
        case PipelineType::Render:
            return "render";
        case PipelineType::Compute:
            return "compute";
        case PipelineType::RayTracing:
            return "ray-tracing";
        default:
            return "-";
        }
    };

    if (m_printReports)
    {
        m_device->printInfo(
            "Shader program %llu: Creating %s pipeline took %.1f ms (cached: %s, cacheSize: %zd)",
            program->m_id,
            getPipelineTypeName(pipelineType),
            Timer::deltaMS(startTime, endTime),
            isCached ? "yes" : "no",
            cacheSize
        );
    }

    if (m_recordReports)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        SLANG_RHI_ASSERT(program->m_id < m_programReports.size());
        ProgramReport& programReport = m_programReports[program->m_id];
        PipelineReport& pipelineReport = programReport.pipelineReports.emplace_back();
        pipelineReport.type = pipelineType;
        pipelineReport.startTime = startTime;
        pipelineReport.endTime = endTime;
        pipelineReport.createTime = Timer::delta(startTime, endTime);
        pipelineReport.isCached = isCached;
        pipelineReport.cacheSize = cacheSize;
    }
}

Result ShaderCompilationReporter::getCompilationReport(ShaderProgram* program, ISlangBlob** outReportBlob)
{
    if (!program || !outReportBlob)
    {
        return SLANG_E_INVALID_ARG;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    if (program->m_id >= m_programReports.size())
    {
        return SLANG_E_NOT_FOUND;
    }
    const ProgramReport& report = m_programReports[program->m_id];

    size_t reportSize = sizeof(CompilationReport);
    reportSize += report.entryPointReports.size() * sizeof(EntryPointReport);
    reportSize += report.pipelineReports.size() * sizeof(PipelineReport);
    Slang::ComPtr<ISlangBlob> reportBlob = OwnedBlob::create(reportSize);
    CompilationReport* dstReport = (CompilationReport*)reportBlob->getBufferPointer();
    EntryPointReport* dstEntryPoints = (EntryPointReport*)(dstReport + 1);
    PipelineReport* dstPipelines = (PipelineReport*)(dstEntryPoints + report.entryPointReports.size());
    writeCompilationReport(dstReport, dstEntryPoints, dstPipelines, report);
    returnComPtr(outReportBlob, reportBlob);
    return SLANG_OK;
}

Result ShaderCompilationReporter::getCompilationReportList(ISlangBlob** outReportListBlob)
{
    if (!outReportListBlob)
    {
        return SLANG_E_INVALID_ARG;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    size_t totalSize = sizeof(CompilationReportList);
    size_t totalEntryPoints = 0;
    for (const auto& report : m_programReports)
    {
        totalSize += sizeof(CompilationReport);
        totalSize += report.entryPointReports.size() * sizeof(EntryPointReport);
        totalSize += report.pipelineReports.size() * sizeof(PipelineReport);
        totalEntryPoints += report.entryPointReports.size();
    }
    Slang::ComPtr<ISlangBlob> reportListBlob = OwnedBlob::create(totalSize);
    CompilationReportList* reportList = (CompilationReportList*)reportListBlob->getBufferPointer();
    CompilationReport* dstReport = (CompilationReport*)(reportList + 1);
    EntryPointReport* dstEntryPoints = (EntryPointReport*)(dstReport + m_programReports.size());
    PipelineReport* dstPipelines = (PipelineReport*)(dstEntryPoints + totalEntryPoints);
    reportList->reports = dstReport;
    reportList->reportCount = (uint32_t)m_programReports.size();
    for (const auto& report : m_programReports)
    {
        writeCompilationReport(dstReport, dstEntryPoints, dstPipelines, report);
        dstReport++;
        dstEntryPoints += report.entryPointReports.size();
        dstPipelines += report.pipelineReports.size();
    }
    returnComPtr(outReportListBlob, reportListBlob);
    return SLANG_OK;
}

void ShaderCompilationReporter::writeCompilationReport(
    CompilationReport* dst,
    EntryPointReport* dstEntryPoints,
    PipelineReport* dstPipelines,
    const ProgramReport& src
)
{
    string::copy_safe(dst->label, sizeof(dst->label), src.label.c_str());
    dst->alive = src.alive;
    dst->entryPointReports = src.entryPointReports.empty() ? nullptr : dstEntryPoints;
    dst->entryPointReportCount = src.entryPointReports.size();
    dst->pipelineReports = src.pipelineReports.empty() ? nullptr : dstPipelines;
    dst->pipelineReportCount = src.pipelineReports.size();

    double createTime = 0.0;
    double compileTime = 0.0;
    double compileSlangTime = 0.0;
    double compileDownstreamTime = 0.0;
    for (const auto& entryPointReport : src.entryPointReports)
    {
        *dstEntryPoints++ = entryPointReport;
        createTime += entryPointReport.createTime;
        compileTime += entryPointReport.compileTime;
        compileSlangTime += entryPointReport.compileSlangTime;
        compileDownstreamTime += entryPointReport.compileDownstreamTime;
    }
    double createPipelineTime = 0.0;
    for (const auto& pipelineReport : src.pipelineReports)
    {
        *dstPipelines++ = pipelineReport;
        createPipelineTime += pipelineReport.createTime;
    }

    dst->createTime = createTime;
    dst->compileTime = compileTime;
    dst->compileSlangTime = compileSlangTime;
    dst->compileDownstreamTime = compileDownstreamTime;
    dst->createPipelineTime = createPipelineTime;
}

} // namespace rhi
