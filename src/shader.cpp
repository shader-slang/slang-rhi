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

    if (m_device->m_shaderCompilationReporter)
    {
        m_device->m_shaderCompilationReporter->registerProgram(this);
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

// ----------------------------------------------------------------------------
// ShaderCompilationReporter
// ----------------------------------------------------------------------------

ShaderCompilationReporter::ShaderCompilationReporter(Device* device)
    : m_device(device)
{
}

void ShaderCompilationReporter::registerProgram(ShaderProgram* program)
{
    m_device->printInfo("Register shader program %p\n", program);
}

void ShaderCompilationReporter::reportGetEntryPointCode(
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
    m_device->printInfo(
        "Get entry point code for shader program %p - %s took %f ms (slang: %f ms, downstream: %f ms, cached: %s, "
        "cacheSize: %zd)\n",
        program,
        entryPointName,
        (endTime - startTime) / 1e6,
        totalTime * 1e3,
        downstreamTime * 1e3,
        isCached ? "yes" : "no",
        cacheSize
    );
}

void ShaderCompilationReporter::reportCreatePipeline(
    ShaderProgram* shaderProgram,
    PipelineType pipelineType,
    TimePoint startTime,
    TimePoint endTime,
    bool isCached,
    size_t cacheSize
)
{
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
    m_device->printInfo(
        "Create %s pipeline for shader program %p took %f ms (cached: %s, cacheSize: %zd)\n",
        getPipelineTypeName(pipelineType),
        shaderProgram,
        (endTime - startTime) / 1e6,
        isCached ? "yes" : "no",
        cacheSize
    );
}

} // namespace rhi
