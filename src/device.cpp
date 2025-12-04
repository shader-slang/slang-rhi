#include "device.h"

#include "rhi-shared.h"
#include "shader.h"
#include "heap.h"

#include <algorithm>
#include <cstdarg>

namespace rhi {

namespace testing {
bool gDebugDisableStateTracking = false;
} // namespace testing

// ----------------------------------------------------------------------------
// ShaderCache
// ----------------------------------------------------------------------------

ShaderComponentID ShaderCache::getComponentId(slang::TypeReflection* type)
{
    ComponentKey key;
    key.typeName = string::from_cstr(type->getName());
    switch (type->getKind())
    {
    case slang::TypeReflection::Kind::Specialized:
    {
        auto baseType = type->getElementType();

        std::string str;
        str += string::from_cstr(baseType->getName());

        auto rawType = (SlangReflectionType*)type;

        str += '<';
        SlangInt argCount = spReflectionType_getSpecializedTypeArgCount(rawType);
        for (SlangInt a = 0; a < argCount; ++a)
        {
            if (a != 0)
                str += ',';
            if (auto rawArgType = spReflectionType_getSpecializedTypeArgType(rawType, a))
            {
                auto argType = (slang::TypeReflection*)rawArgType;
                str += string::from_cstr(argType->getName());
            }
        }
        str += '>';
        key.typeName = std::move(str);
        key.updateHash();
        return getComponentId(key);
    }
        // TODO: collect specialization arguments and append them to `key`.
        SLANG_RHI_UNIMPLEMENTED("specialized type");
    default:
        break;
    }
    key.updateHash();
    return getComponentId(key);
}

ShaderComponentID ShaderCache::getComponentId(std::string_view name)
{
    ComponentKey key;
    key.typeName = name;
    key.updateHash();
    return getComponentId(key);
}

ShaderComponentID ShaderCache::getComponentId(ComponentKey key)
{
    auto it = componentIds.find(key);
    if (it != componentIds.end())
        return it->second;
    ShaderComponentID resultId = static_cast<ShaderComponentID>(componentIds.size());
    componentIds.emplace(key, resultId);
    return resultId;
}

RefPtr<Pipeline> ShaderCache::getSpecializedPipeline(PipelineKey programKey)
{
    auto it = specializedPipelines.find(programKey);
    if (it != specializedPipelines.end())
        return it->second;
    return nullptr;
}

void ShaderCache::addSpecializedPipeline(PipelineKey key, RefPtr<Pipeline> specializedPipeline)
{
    specializedPipelines[key] = specializedPipeline;
}

void ShaderCache::free()
{
    componentIds = decltype(componentIds)();
    specializedPipelines = decltype(specializedPipelines)();
}

// ----------------------------------------------------------------------------
// Device
// ----------------------------------------------------------------------------

void Device::printMessage(DebugMessageType type, DebugMessageSource source, const char* message, ...)
{
    va_list args;
    va_start(args, message);
    char buffer[4096];
    vsnprintf(buffer, sizeof(buffer), message, args);
    va_end(args);
    handleMessage(type, source, buffer);
}

void Device::printInfo(const char* message, ...)
{
    va_list args;
    va_start(args, message);
    char buffer[4096];
    vsnprintf(buffer, sizeof(buffer), message, args);
    va_end(args);
    handleMessage(DebugMessageType::Info, DebugMessageSource::Layer, buffer);
}

void Device::printWarning(const char* message, ...)
{
    va_list args;
    va_start(args, message);
    char buffer[4096];
    vsnprintf(buffer, sizeof(buffer), message, args);
    va_end(args);
    handleMessage(DebugMessageType::Warning, DebugMessageSource::Layer, buffer);
}

void Device::printError(const char* message, ...)
{
    va_list args;
    va_start(args, message);
    char buffer[4096];
    vsnprintf(buffer, sizeof(buffer), message, args);
    va_end(args);
    handleMessage(DebugMessageType::Error, DebugMessageSource::Layer, buffer);
}

Result Device::createShaderObject(ShaderObjectLayout* layout, ShaderObject** outObject)
{
    return ShaderObject::create(this, layout, outObject);
}

Result Device::createRootShaderObject(ShaderProgram* program, RootShaderObject** outObject)
{
    return RootShaderObject::create(this, program, outObject);
}

Result Device::getSpecializedProgram(
    ShaderProgram* program,
    const ExtendedShaderObjectTypeList& specializationArgs,
    ShaderProgram** outSpecializedProgram
)
{
    // TODO make thread-safe
    SpecializationKey key(specializationArgs);
    auto it = program->m_specializedPrograms.find(key);
    if (it != program->m_specializedPrograms.end())
    {
        returnRefPtr(outSpecializedProgram, it->second);
        return SLANG_OK;
    }
    else
    {
        RefPtr<ShaderProgram> specializedProgram;
        SLANG_RETURN_ON_FAIL(specializeProgram(program, specializationArgs, specializedProgram.writeRef()));
        program->m_specializedPrograms[key] = specializedProgram;
        // Program is owned by the cache (which is owned by the device).
        specializedProgram->breakStrongReferenceToDevice();
        returnRefPtr(outSpecializedProgram, specializedProgram);
        return SLANG_OK;
    }
}


Result Device::specializeProgram(
    ShaderProgram* program,
    const ExtendedShaderObjectTypeList& specializationArgs,
    ShaderProgram** outSpecializedProgram
)
{
    ComPtr<slang::IComponentType> specializedComponentType;
    ComPtr<slang::IBlob> diagnosticBlob;
    Result result = program->linkedProgram->specialize(
        specializationArgs.components.data(),
        specializationArgs.getCount(),
        specializedComponentType.writeRef(),
        diagnosticBlob.writeRef()
    );
    if (diagnosticBlob)
    {
        handleMessage(
            result == SLANG_OK ? DebugMessageType::Warning : DebugMessageType::Error,
            DebugMessageSource::Slang,
            (char*)diagnosticBlob->getBufferPointer()
        );
    }
    SLANG_RETURN_ON_FAIL(result);

    // Now create the specialized shader program using compiled binaries.
    RefPtr<ShaderProgram> specializedProgram;
    ShaderProgramDesc programDesc = program->m_desc;
    programDesc.slangGlobalScope = specializedComponentType;

    if (programDesc.linkingStyle == LinkingStyle::SingleProgram)
    {
        // When linking style is SingleProgram, the specialized global scope already contains
        // entry-points, so we do not need to supply them again when creating the specialized
        // pipeline.
        programDesc.slangEntryPointCount = 0;
    }
    SLANG_RETURN_ON_FAIL(createShaderProgram(programDesc, (IShaderProgram**)specializedProgram.writeRef()));
    returnRefPtr(outSpecializedProgram, specializedProgram);
    return SLANG_OK;
}

Result Device::getConcretePipeline(
    Pipeline* pipeline,
    ExtendedShaderObjectTypeList* specializationArgs,
    Pipeline*& outPipeline
)
{
    // Virtual pipelines are created due to 2 reasons:
    //
    // 1) When the user creates a pipeline with a program that has undefined specialization arguments, there is no way
    // to compile target code to create the backend pipeline resources during creation time. Therefore, a virtual
    // pipeline is created which needs to be specialized later (during dispatch time, when specialization arguments are
    // known).
    //
    // 2) The user requests deferred compilation of target code when creating a pipeline, in which case the
    // backend resources are not created until they are needed (during dispatch time).
    //
    // This code handles both these cases. For the specialization case, we query the device's pipeline cache for
    // a specialized pipeline that matches the specialization arguments provided. If not found, we specialize
    // the program, compile the target code, create the backend resources and cache the specialized pipeline
    // For the deferred case, we simply create the backend resources and keep a reference to the concrete pipeline
    // in the virtual one for later use.

    // If this is already a concrete pipeline, then we are done.
    if (!pipeline->isVirtual())
    {
        outPipeline = pipeline;
        return SLANG_OK;
    }

    // Return early if we previously created a concrete pipeline for this virtual pipeline.
    outPipeline = pipeline->getConcretePipeline();
    if (outPipeline)
    {
        return SLANG_OK;
    }

    // Check if program is specializable (if it has specialization arguments).
    bool isSpecializable = pipeline->m_program->isSpecializable();

    // If the pipeline is specializable, collect specialization arguments from bound shader objects
    // and lookup the cache.
    PipelineKey pipelineKey;
    pipelineKey.pipeline = pipeline;
    if (isSpecializable)
    {
        if (!specializationArgs)
        {
            return SLANG_FAIL;
        }
        for (const auto& componentID : specializationArgs->componentIDs)
        {
            pipelineKey.specializationArgs.push_back(componentID);
        }
        pipelineKey.updateHash();
        outPipeline = m_shaderCache.getSpecializedPipeline(pipelineKey);
        if (outPipeline)
        {
            return SLANG_OK;
        }
    }

    // At this point we need to create a new concrete pipeline.
    RefPtr<ShaderProgram> program = pipeline->m_program;
    if (isSpecializable)
    {
        RefPtr<ShaderProgram> specializedProgram;
        SLANG_RETURN_ON_FAIL(specializeProgram(program, *specializationArgs, specializedProgram.writeRef()));
        program = specializedProgram;
    }

    // Ensure sure shaders are compiled.
    SLANG_RETURN_ON_FAIL(program->compileShaders(this));

    // Create a new concrete pipeline.
    RefPtr<Pipeline> concretePipeline;
    switch (pipeline->getType())
    {
    case PipelineType::Render:
    {
        RenderPipelineDesc desc = checked_cast<VirtualRenderPipeline*>(pipeline)->m_desc;
        desc.program = program;
        ComPtr<IRenderPipeline> renderPipeline;
        SLANG_RETURN_ON_FAIL(createRenderPipeline2(desc, renderPipeline.writeRef()));
        concretePipeline = checked_cast<RenderPipeline*>(renderPipeline.get());
        break;
    }
    case PipelineType::Compute:
    {
        ComputePipelineDesc desc = checked_cast<VirtualComputePipeline*>(pipeline)->m_desc;
        desc.program = program;
        ComPtr<IComputePipeline> computePipeline;
        SLANG_RETURN_ON_FAIL(createComputePipeline2(desc, computePipeline.writeRef()));
        concretePipeline = checked_cast<ComputePipeline*>(computePipeline.get());
        break;
    }
    case PipelineType::RayTracing:
    {
        RayTracingPipelineDesc desc = checked_cast<VirtualRayTracingPipeline*>(pipeline)->m_desc;
        desc.program = program;
        ComPtr<IRayTracingPipeline> rayTracingPipeline;
        SLANG_RETURN_ON_FAIL(createRayTracingPipeline2(desc, rayTracingPipeline.writeRef()));
        concretePipeline = checked_cast<RayTracingPipeline*>(rayTracingPipeline.get());
        break;
    }
    }

    if (isSpecializable)
    {
        // Cache the specialized pipeline for later use.
        m_shaderCache.addSpecializedPipeline(pipelineKey, concretePipeline);
        // Pipeline is owned by the cache.
        concretePipeline->breakStrongReferenceToDevice();
        // Program is owned by the specialized pipeline (which is owned by the cache).
        concretePipeline->m_program->breakStrongReferenceToDevice();
    }
    else
    {
        // Store the concrete pipeline in the virtual one.
        pipeline->setConcretePipeline(concretePipeline);
    }

    outPipeline = concretePipeline;
    return SLANG_OK;
}

Result Device::createRenderPipeline2(const RenderPipelineDesc& desc, IRenderPipeline** outPipeline)
{
    SLANG_UNUSED(desc);
    SLANG_UNUSED(outPipeline);
    return SLANG_E_NOT_AVAILABLE;
}

Result Device::createComputePipeline2(const ComputePipelineDesc& desc, IComputePipeline** outPipeline)
{
    SLANG_UNUSED(desc);
    SLANG_UNUSED(outPipeline);
    return SLANG_E_NOT_AVAILABLE;
}

Result Device::createRayTracingPipeline2(const RayTracingPipelineDesc& desc, IRayTracingPipeline** outPipeline)
{
    SLANG_UNUSED(desc);
    SLANG_UNUSED(outPipeline);
    return SLANG_E_NOT_AVAILABLE;
}

Result Device::getEntryPointCodeFromShaderCache(
    ShaderProgram* program,
    slang::IComponentType* componentType,
    const char* entryPointName,
    uint32_t entryPointIndex,
    uint32_t targetIndex,
    slang::IBlob** outCode,
    slang::IBlob** outDiagnostics
)
{
    TimePoint startTime = Timer::now();
    ComPtr<ISlangBlob> codeBlob;
    ComPtr<ISlangBlob> hashBlob;

    if (m_persistentShaderCache)
    {
        // Hash all relevant state for generating the entry point shader code to use as a key
        // for the shader cache.
        componentType->getEntryPointHash(entryPointIndex, targetIndex, hashBlob.writeRef());

        // Query the shader cache.
        if (m_persistentShaderCache->queryCache(hashBlob, codeBlob.writeRef()) == SLANG_OK)
        {
            if (m_shaderCompilationReporter)
            {
                m_shaderCompilationReporter->reportCompileEntryPoint(
                    program,
                    entryPointName,
                    startTime,
                    Timer::now(),
                    0.0,
                    0.0,
                    true,
                    codeBlob->getBufferSize()
                );
            }

            returnComPtr(outCode, codeBlob);
            return SLANG_OK;
        }
    }

    // Cached entry not found, generate the code and measure compilation time.
    double startTotalTime, endTotalTime;
    double startDownstreamTime, endDownstreamTime;
    componentType->getSession()->getGlobalSession()->getCompilerElapsedTime(&startTotalTime, &startDownstreamTime);
    SLANG_RETURN_ON_FAIL(
        componentType->getEntryPointCode(entryPointIndex, targetIndex, codeBlob.writeRef(), outDiagnostics)
    );
    componentType->getSession()->getGlobalSession()->getCompilerElapsedTime(&endTotalTime, &endDownstreamTime);

    // Write the generated code to the shader cache if available.
    if (m_persistentShaderCache)
    {
        m_persistentShaderCache->writeCache(hashBlob, codeBlob);
    }

    // Report compilation time.
    if (m_shaderCompilationReporter)
    {
        m_shaderCompilationReporter->reportCompileEntryPoint(
            program,
            entryPointName,
            startTime,
            Timer::now(),
            endTotalTime - startTotalTime,
            endDownstreamTime - startDownstreamTime,
            false,
            codeBlob->getBufferSize()
        );
    }

    returnComPtr(outCode, codeBlob);
    return SLANG_OK;
}

Result Device::queryInterface(const SlangUUID& uuid, void** outObject)
{
    *outObject = getInterface(uuid);
    return SLANG_OK;
}

IDevice* Device::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IDevice::getTypeGuid())
        return static_cast<IDevice*>(this);
    return nullptr;
}

Result Device::initialize(const DeviceDesc& desc)
{
    m_info = {};
    m_info.deviceType = desc.deviceType;

    m_featureSet.fill(false);
    m_capabilitySet.fill(false);
    m_formatSupport.fill(FormatSupport::None);

    m_debugCallback = desc.debugCallback ? desc.debugCallback : NullDebugCallback::getInstance();

    if (desc.enableCompilationReports)
    {
        m_shaderCompilationReporter = new ShaderCompilationReporter(this);
    }

    m_persistentShaderCache = desc.persistentShaderCache;
    m_persistentPipelineCache = desc.persistentPipelineCache;

    m_uploadHeap.initialize(this, desc.stagingHeapPageSize, MemoryType::Upload);
    m_readbackHeap.initialize(this, desc.stagingHeapPageSize, MemoryType::ReadBack);

    return SLANG_OK;
}

void Device::addFeature(Feature feature)
{
    SLANG_RHI_ASSERT(size_t(feature) < size_t(Feature::_Count));
    m_featureSet[size_t(feature)] = true;
}

void Device::addCapability(Capability capability)
{
    SLANG_RHI_ASSERT(size_t(capability) < size_t(Capability::_Count));
    m_capabilitySet[size_t(capability)] = true;
}

std::vector<Capability> Device::getCapabilities()
{
    std::vector<Capability> capabilities;
    for (size_t i = 0; i < m_capabilitySet.size(); i++)
    {
        if (m_capabilitySet[i])
        {
            capabilities.push_back(Capability(i));
        }
    }
    return capabilities;
}

Result Device::getNativeDeviceHandles(DeviceNativeHandles* outHandles)
{
    return SLANG_OK;
}

Result Device::getFeatures(uint32_t* outFeatureCount, Feature* outFeatures)
{
    if (!outFeatureCount)
    {
        return SLANG_E_INVALID_ARG;
    }
    if (outFeatures)
    {
        uint32_t featureIndex = 0;
        for (size_t i = 0; i < m_featureSet.size(); i++)
        {
            if (m_featureSet[i])
            {
                if (featureIndex < *outFeatureCount)
                {
                    outFeatures[featureIndex++] = Feature(i);
                }
                else
                {
                    return SLANG_E_INVALID_ARG;
                }
            }
        }
    }
    else
    {
        uint32_t featureCount = 0;
        for (size_t i = 0; i < m_featureSet.size(); i++)
        {
            featureCount += m_featureSet[i] ? 1 : 0;
        }
        *outFeatureCount = featureCount;
    }
    return SLANG_OK;
}

bool Device::hasFeature(Feature feature)
{
    return size_t(feature) < size_t(Feature::_Count) ? m_featureSet[size_t(feature)] : false;
}

bool Device::hasFeature(const char* feature)
{
#define SLANG_RHI_FEATURES_X(id, name) {name, Feature::id},
    static const std::unordered_map<std::string_view, Feature> kFeatureNameMap = {
        SLANG_RHI_FEATURES(SLANG_RHI_FEATURES_X)
    };
#undef SLANG_RHI_FEATURES_X

    auto it = kFeatureNameMap.find(feature);
    if (it != kFeatureNameMap.end())
    {
        return hasFeature(it->second);
    }
    return false;
}

Result Device::getCapabilities(uint32_t* outCapabilityCount, Capability* outCapabilities)
{
    if (!outCapabilityCount)
    {
        return SLANG_E_INVALID_ARG;
    }
    if (outCapabilities)
    {
        uint32_t capabilityIndex = 0;
        for (size_t i = 0; i < m_capabilitySet.size(); i++)
        {
            if (m_capabilitySet[i])
            {
                if (capabilityIndex < *outCapabilityCount)
                {
                    outCapabilities[capabilityIndex++] = Capability(i);
                }
                else
                {
                    return SLANG_E_INVALID_ARG;
                }
            }
        }
    }
    else
    {
        uint32_t capabilityCount = 0;
        for (size_t i = 0; i < m_capabilitySet.size(); i++)
        {
            capabilityCount += m_capabilitySet[i] ? 1 : 0;
        }
        *outCapabilityCount = capabilityCount;
    }
    return SLANG_OK;
}

bool Device::hasCapability(Capability capability)
{
    return size_t(capability) < size_t(Capability::_Count) ? m_capabilitySet[size_t(capability)] : false;
}

bool Device::hasCapability(const char* capability)
{
#define SLANG_RHI_CAPABILITIES_X(id) {#id, Capability::id},
    static const std::unordered_map<std::string_view, Capability> kCapabilityMap = {
        SLANG_RHI_CAPABILITIES(SLANG_RHI_CAPABILITIES_X)
    };
#undef SLANG_RHI_CAPABILITIES_X

    auto it = kCapabilityMap.find(capability);
    if (it != kCapabilityMap.end())
    {
        return hasCapability(it->second);
    }
    return false;
}

Result Device::getFormatSupport(Format format, FormatSupport* outFormatSupport)
{
    if (size_t(format) >= m_formatSupport.size() || !outFormatSupport)
    {
        return SLANG_E_INVALID_ARG;
    }
    *outFormatSupport = m_formatSupport[size_t(format)];
    return SLANG_OK;
}

Result Device::getSlangSession(slang::ISession** outSlangSession)
{
    *outSlangSession = m_slangContext.session.get();
    m_slangContext.session->addRef();
    return SLANG_OK;
}

Result Device::createTextureFromNativeHandle(NativeHandle handle, const TextureDesc& desc, ITexture** outTexture)
{
    SLANG_UNUSED(handle);
    SLANG_UNUSED(desc);
    SLANG_UNUSED(outTexture);
    return SLANG_E_NOT_AVAILABLE;
}

Result Device::createTextureFromSharedHandle(
    NativeHandle handle,
    const TextureDesc& desc,
    const Size size,
    ITexture** outTexture
)
{
    SLANG_UNUSED(handle);
    SLANG_UNUSED(desc);
    SLANG_UNUSED(size);
    SLANG_UNUSED(outTexture);
    return SLANG_E_NOT_AVAILABLE;
}

Result Device::createBufferFromNativeHandle(NativeHandle handle, const BufferDesc& desc, IBuffer** outBuffer)
{
    SLANG_UNUSED(handle);
    SLANG_UNUSED(desc);
    SLANG_UNUSED(outBuffer);
    return SLANG_E_NOT_AVAILABLE;
}

Result Device::createBufferFromSharedHandle(NativeHandle handle, const BufferDesc& desc, IBuffer** outBuffer)
{
    SLANG_UNUSED(handle);
    SLANG_UNUSED(desc);
    SLANG_UNUSED(outBuffer);
    return SLANG_E_NOT_AVAILABLE;
}

Result Device::createInputLayout(const InputLayoutDesc& desc, IInputLayout** outLayout)
{
    SLANG_UNUSED(desc);
    SLANG_UNUSED(outLayout);
    return SLANG_E_NOT_AVAILABLE;
}

Result Device::createRenderPipeline(const RenderPipelineDesc& desc, IRenderPipeline** outPipeline)
{
    ShaderProgram* program = checked_cast<ShaderProgram*>(desc.program);
    bool createVirtual = desc.deferTargetCompilation | program->isSpecializable();
    if (createVirtual)
    {
        RefPtr<VirtualRenderPipeline> pipeline = new VirtualRenderPipeline(this, desc);
        returnComPtr(outPipeline, pipeline);
        return SLANG_OK;
    }
    else
    {
        SLANG_RETURN_ON_FAIL(program->compileShaders(this));
        return createRenderPipeline2(desc, outPipeline);
    }
}

Result Device::createComputePipeline(const ComputePipelineDesc& desc, IComputePipeline** outPipeline)
{
    ShaderProgram* program = checked_cast<ShaderProgram*>(desc.program);
    bool createVirtual = desc.deferTargetCompilation | program->isSpecializable();
    if (createVirtual)
    {
        RefPtr<VirtualComputePipeline> pipeline = new VirtualComputePipeline(this, desc);
        returnComPtr(outPipeline, pipeline);
        return SLANG_OK;
    }
    else
    {
        SLANG_RETURN_ON_FAIL(program->compileShaders(this));
        return createComputePipeline2(desc, outPipeline);
    }
}

Result Device::createRayTracingPipeline(const RayTracingPipelineDesc& desc, IRayTracingPipeline** outPipeline)
{
    ShaderProgram* program = checked_cast<ShaderProgram*>(desc.program);
    bool createVirtual = desc.deferTargetCompilation | program->isSpecializable();
    if (createVirtual)
    {
        RefPtr<VirtualRayTracingPipeline> pipeline = new VirtualRayTracingPipeline(this, desc);
        returnComPtr(outPipeline, pipeline);
        return SLANG_OK;
    }
    else
    {
        SLANG_RETURN_ON_FAIL(program->compileShaders(this));
        return createRayTracingPipeline2(desc, outPipeline);
    }
}

Result Device::getCompilationReportList(ISlangBlob** outReportListBlob)
{
    if (!m_shaderCompilationReporter)
    {
        return SLANG_E_NOT_AVAILABLE;
    }
    return m_shaderCompilationReporter->getCompilationReportList(outReportListBlob);
}

Result Device::createShaderObject(
    slang::ISession* slangSession,
    slang::TypeReflection* type,
    ShaderObjectContainerType container,
    IShaderObject** outObject
)
{
    if (slangSession == nullptr)
        slangSession = m_slangContext.session.get();
    RefPtr<ShaderObjectLayout> shaderObjectLayout;
    SLANG_RETURN_ON_FAIL(getShaderObjectLayout(slangSession, type, container, shaderObjectLayout.writeRef()));
    RefPtr<ShaderObject> shaderObject;
    SLANG_RETURN_ON_FAIL(createShaderObject(shaderObjectLayout, shaderObject.writeRef()));
    returnComPtr(outObject, shaderObject);
    return SLANG_OK;
}

Result Device::createShaderObjectFromTypeLayout(slang::TypeLayoutReflection* typeLayout, IShaderObject** outObject)
{
    RefPtr<ShaderObjectLayout> shaderObjectLayout;
    SLANG_RETURN_ON_FAIL(getShaderObjectLayout(m_slangContext.session, typeLayout, shaderObjectLayout.writeRef()));
    RefPtr<ShaderObject> shaderObject;
    SLANG_RETURN_ON_FAIL(createShaderObject(shaderObjectLayout, shaderObject.writeRef()));
    returnComPtr(outObject, shaderObject);
    return SLANG_OK;
}

Result Device::createRootShaderObject(IShaderProgram* program, IShaderObject** outObject)
{
    ShaderProgram* shaderProgram = checked_cast<ShaderProgram*>(program);
    RefPtr<RootShaderObject> rootShaderObject;
    SLANG_RETURN_ON_FAIL(createRootShaderObject(shaderProgram, rootShaderObject.writeRef()));
    returnComPtr(outObject, rootShaderObject);
    return SLANG_OK;
}

Result Device::getAccelerationStructureSizes(
    const AccelerationStructureBuildDesc& desc,
    AccelerationStructureSizes* outSizes
)
{
    SLANG_UNUSED(desc);
    SLANG_UNUSED(outSizes);
    return SLANG_E_NOT_AVAILABLE;
}

Result Device::getClusterOperationSizes(const ClusterOperationParams& params, ClusterOperationSizes* outSizes)
{
    SLANG_UNUSED(params);
    SLANG_UNUSED(outSizes);
    return SLANG_E_NOT_AVAILABLE;
}

Result Device::createAccelerationStructure(
    const AccelerationStructureDesc& desc,
    IAccelerationStructure** outAccelerationStructure
)
{
    SLANG_UNUSED(desc);
    SLANG_UNUSED(outAccelerationStructure);
    return SLANG_E_NOT_AVAILABLE;
}

Result Device::createShaderTable(const ShaderTableDesc& desc, IShaderTable** outTable)
{
    SLANG_UNUSED(desc);
    SLANG_UNUSED(outTable);
    return SLANG_E_NOT_AVAILABLE;
}

Result Device::createFence(const FenceDesc& desc, IFence** outFence)
{
    SLANG_UNUSED(desc);
    *outFence = nullptr;
    return SLANG_E_NOT_AVAILABLE;
}

Result Device::waitForFences(
    uint32_t fenceCount,
    IFence** fences,
    const uint64_t* fenceValues,
    bool waitForAll,
    uint64_t timeout
)
{
    SLANG_UNUSED(fenceCount);
    SLANG_UNUSED(fences);
    SLANG_UNUSED(fenceValues);
    SLANG_UNUSED(waitForAll);
    SLANG_UNUSED(timeout);
    return SLANG_E_NOT_AVAILABLE;
}

Result Device::createHeap(const HeapDesc& desc, IHeap** outHeap)
{
    SLANG_UNUSED(desc);
    SLANG_UNUSED(outHeap);
    return SLANG_E_NOT_AVAILABLE;
}

Result Device::readTexture(
    ITexture* texture,
    uint32_t layer,
    uint32_t mip,
    const SubresourceLayout& layout,
    void* outData
)
{
    ComPtr<ICommandQueue> queue;
    SLANG_RETURN_ON_FAIL(getQueue(QueueType::Graphics, queue.writeRef()));

    ComPtr<ICommandEncoder> commandEncoder;
    SLANG_RETURN_ON_FAIL(queue->createCommandEncoder(commandEncoder.writeRef()));

    StagingHeap::Allocation stagingAllocation;
    SLANG_RETURN_ON_FAIL(m_readbackHeap.alloc(layout.sizeInBytes, {}, &stagingAllocation));

    commandEncoder->copyTextureToBuffer(
        stagingAllocation.getBuffer(),
        stagingAllocation.getOffset(),
        layout.sizeInBytes,
        layout.rowPitch,
        texture,
        layer,
        mip,
        {0, 0, 0},
        {layout.size.width, layout.size.height, layout.size.depth}
    );

    SLANG_RETURN_ON_FAIL(queue->submit(commandEncoder->finish()));
    SLANG_RETURN_ON_FAIL(queue->waitOnHost());

    void* mappedData;
    SLANG_RETURN_ON_FAIL(m_readbackHeap.map(stagingAllocation, &mappedData));

    std::memcpy(outData, mappedData, layout.sizeInBytes);

    SLANG_RETURN_ON_FAIL(m_readbackHeap.unmap(stagingAllocation));

    m_readbackHeap.free(stagingAllocation);

    return SLANG_OK;
}

Result Device::readTexture(
    ITexture* texture,
    uint32_t layer,
    uint32_t mip,
    ISlangBlob** outBlob,
    SubresourceLayout* outLayout
)
{
    SubresourceLayout layout;
    SLANG_RETURN_ON_FAIL(texture->getSubresourceLayout(mip, &layout));

    auto blob = OwnedBlob::create(layout.sizeInBytes);

    SLANG_RETURN_ON_FAIL(readTexture(texture, layer, mip, layout, (void*)blob->getBufferPointer()));

    if (outLayout)
        *outLayout = layout;

    returnComPtr(outBlob, blob);
    return SLANG_OK;
}

Result Device::readBuffer(IBuffer* buffer, Offset offset, Size size, ISlangBlob** outBlob)
{
    auto blob = OwnedBlob::create(size);
    SLANG_RETURN_ON_FAIL(readBuffer(buffer, offset, size, (void*)blob->getBufferPointer()));
    returnComPtr(outBlob, blob);
    return SLANG_OK;
}

Result Device::getTextureAllocationInfo(const TextureDesc& desc, Size* outSize, Size* outAlignment)
{
    SLANG_UNUSED(desc);
    *outSize = 0;
    *outAlignment = 0;
    return SLANG_E_NOT_AVAILABLE;
}

Result Device::getTextureRowAlignment(Format format, Size* outAlignment)
{
    *outAlignment = 0;
    return SLANG_E_NOT_AVAILABLE;
}

Result Device::createSurface(WindowHandle windowHandle, ISurface** outSurface)
{
    SLANG_UNUSED(windowHandle);
    *outSurface = nullptr;
    return SLANG_E_NOT_AVAILABLE;
}

Result Device::getCooperativeVectorProperties(CooperativeVectorProperties* properties, uint32_t* propertiesCount)
{
    if (!propertiesCount)
    {
        return SLANG_E_INVALID_ARG;
    }
    if (m_cooperativeVectorProperties.empty())
    {
        return SLANG_E_NOT_AVAILABLE;
    }
    if (properties)
    {
        uint32_t count = min(*propertiesCount, uint32_t(m_cooperativeVectorProperties.size()));
        ::memcpy(properties, m_cooperativeVectorProperties.data(), count * sizeof(CooperativeVectorProperties));
        Result result = count == *propertiesCount ? SLANG_OK : SLANG_E_BUFFER_TOO_SMALL;
        *propertiesCount = count;
        return result;
    }
    else
    {
        *propertiesCount = uint32_t(m_cooperativeVectorProperties.size());
        return SLANG_OK;
    }
}

Result Device::getCooperativeVectorMatrixSize(
    uint32_t rowCount,
    uint32_t colCount,
    CooperativeVectorComponentType componentType,
    CooperativeVectorMatrixLayout layout,
    size_t rowColumnStride,
    size_t* outSize
)
{
    SLANG_UNUSED(rowCount);
    SLANG_UNUSED(colCount);
    SLANG_UNUSED(componentType);
    SLANG_UNUSED(layout);
    SLANG_UNUSED(rowColumnStride);
    SLANG_UNUSED(outSize);
    return SLANG_E_NOT_AVAILABLE;
}

Result Device::convertCooperativeVectorMatrix(
    void* dstBuffer,
    size_t dstBufferSize,
    const CooperativeVectorMatrixDesc* dstDescs,
    const void* srcBuffer,
    size_t srcBufferSize,
    const CooperativeVectorMatrixDesc* srcDescs,
    uint32_t matrixCount
)
{
    SLANG_UNUSED(dstBuffer);
    SLANG_UNUSED(dstBufferSize);
    SLANG_UNUSED(dstDescs);
    SLANG_UNUSED(srcBuffer);
    SLANG_UNUSED(srcBufferSize);
    SLANG_UNUSED(srcDescs);
    SLANG_UNUSED(matrixCount);
    return SLANG_E_NOT_AVAILABLE;
}

Result Device::reportHeaps(HeapReport* heapReports, uint32_t* heapCount)
{
    if (!heapCount)
        return SLANG_E_INVALID_ARG;

    uint32_t totalHeapCount = static_cast<uint32_t>(m_globalHeaps.size());

    // If only querying count, return early
    if (!heapReports)
    {
        *heapCount = totalHeapCount;
        return SLANG_OK;
    }

    // If buffer is provided, it must be large enough
    if (*heapCount < totalHeapCount)
        return SLANG_E_BUFFER_TOO_SMALL;

    // Fill heap reports
    for (uint32_t i = 0; i < totalHeapCount; i++)
    {
        SLANG_RETURN_ON_FAIL(m_globalHeaps[i]->report(&heapReports[i]));
    }

    *heapCount = totalHeapCount;
    return SLANG_OK;
}

Result Device::flushHeaps()
{
    for (Heap* heap : m_globalHeaps)
    {
        SLANG_RETURN_ON_FAIL(heap->flush());
    }
    return SLANG_OK;
}

Result Device::getShaderObjectLayout(
    slang::ISession* session,
    slang::TypeReflection* type,
    ShaderObjectContainerType container,
    ShaderObjectLayout** outLayout
)
{
    switch (container)
    {
    case ShaderObjectContainerType::StructuredBuffer:
        type = session->getContainerType(type, slang::ContainerType::StructuredBuffer);
        break;
    case ShaderObjectContainerType::Array:
        type = session->getContainerType(type, slang::ContainerType::UnsizedArray);
        break;
    default:
        break;
    }

    auto typeLayout = session->getTypeLayout(type);
    SLANG_RETURN_ON_FAIL(getShaderObjectLayout(session, typeLayout, outLayout));
    (*outLayout)->m_slangSession = session;
    return SLANG_OK;
}

Result Device::getShaderObjectLayout(
    slang::ISession* session,
    slang::TypeLayoutReflection* typeLayout,
    ShaderObjectLayout** outLayout
)
{
    RefPtr<ShaderObjectLayout> shaderObjectLayout;
    auto it = m_shaderObjectLayoutCache.find(typeLayout);
    if (it != m_shaderObjectLayoutCache.end())
    {
        shaderObjectLayout = it->second;
    }
    else
    {
        SLANG_RETURN_ON_FAIL(createShaderObjectLayout(session, typeLayout, shaderObjectLayout.writeRef()));
        m_shaderObjectLayoutCache.emplace(typeLayout, shaderObjectLayout);
    }
    *outLayout = shaderObjectLayout.detach();
    return SLANG_OK;
}

} // namespace rhi
