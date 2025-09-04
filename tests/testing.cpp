#include "testing.h"
#include "shader-cache.h"
#include "core/platform.h"
#include <algorithm>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <map>
#include <string>
#include <fstream>

#define ENABLE_RENDERDOC 0
#define DEBUG_SPIRV 0
#define DUMP_INTERMEDIATES 0
#define ENABLE_SHADER_CACHE 0

#if ENABLE_RENDERDOC
#include <renderdoc_app.h>
#endif

namespace rhi::testing {

static std::map<DeviceType, ComPtr<IDevice>> gCachedDevices;
static ShaderCache gShaderCache;

// Temp directory to create files for teting in.
static std::filesystem::path gTestTempDirectory;

// Calculates a files sytem compatible date string formatted YYYY-MM-DD-hh-mm-ss.
static std::string buildCurrentDateString()
{
    time_t now;
    time(&now);
    struct tm tm;
#if SLANG_WINDOWS_FAMILY
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    char result[128];
    std::strftime(result, sizeof(result), "%Y-%m-%d-%H-%M-%S", &tm);
    return result;
}

std::string getTestTempDirectory()
{
    if (gTestTempDirectory == "")
    {
        std::string datetime_str = buildCurrentDateString();
        gTestTempDirectory = std::filesystem::current_path() / ".test_temp" / datetime_str;
        std::filesystem::create_directories(gTestTempDirectory);
    }
    return gTestTempDirectory.string();
}

std::string getSuiteTempDirectory()
{
    auto path = std::filesystem::path(getTestTempDirectory()) / getCurrentTestSuiteName();
    std::filesystem::create_directories(path);
    return path.string();
}

std::string getCaseTempDirectory()
{
    auto path = std::filesystem::path(getTestTempDirectory()) / getCurrentTestSuiteName() / getCurrentTestCaseName();
    std::filesystem::create_directories(path);
    return path.string();
}

void cleanupTestTempDirectories()
{
    remove_all(gTestTempDirectory);
}

std::string readFile(std::string_view path)
{
    std::ifstream file(std::string(path).c_str());
    if (!file.is_open())
        return "";
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return content;
}

void writeFile(std::string_view path, const void* data, size_t size)
{
    std::ofstream file(path.data(), std::ios::binary);
    file.write((const char*)data, size);
}

class CaptureDebugCallback : public IDebugCallback
{
public:
    std::string output;

    void clear() { output.clear(); }

    virtual SLANG_NO_THROW void SLANG_MCALL handleMessage(
        DebugMessageType type,
        DebugMessageSource source,
        const char* message
    ) override
    {
        switch (type)
        {
        case DebugMessageType::Info:
            output += "[Info] ";
            break;
        case DebugMessageType::Warning:
            output += "[Warning] ";
            break;
        case DebugMessageType::Error:
            output += "[Error] ";
            break;
        default:
            break;
        }
        switch (source)
        {
        case DebugMessageSource::Layer:
            output += "[Layer] ";
            break;
        case DebugMessageSource::Driver:
            output += "[Driver] ";
            break;
        case DebugMessageSource::Slang:
            output += "[Slang] ";
            break;
        default:
            break;
        }
        output += message;
        output += "\n";
    }
};

static CaptureDebugCallback sCaptureDebugCallback;

class DebugCallback : public IDebugCallback
{
public:
    bool shouldIgnoreError(DebugMessageType type, DebugMessageSource source, const char* message)
    {
        // These 2 messages pop up as the vulkan validation layer doesn't pick up on CoopVec yet
        if (strstr(message, "VK_NV_cooperative_vector is not supported by this layer"))
            return true;
        if (strstr(message, "includes a structure with unknown VkStructureType (1000491000)"))
            return true;

        // Redundant warning about old architectures
        if (strstr(message, "nvrtc: warning : Architectures prior to"))
            return true;

        return false;
    }


    virtual SLANG_NO_THROW void SLANG_MCALL handleMessage(
        DebugMessageType type,
        DebugMessageSource source,
        const char* message
    ) override
    {
        if (!doctest::is_running_in_test)
            return;

        doctest::String msg;
        switch (type)
        {
        case DebugMessageType::Info:
            msg += "[Info] ";
            break;
        case DebugMessageType::Warning:
            msg += "[Warning] ";
            break;
        case DebugMessageType::Error:
            msg += "[Error] ";
            break;
        default:
            break;
        }
        switch (source)
        {
        case DebugMessageSource::Layer:
            msg += "[Layer] ";
            break;
        case DebugMessageSource::Driver:
            msg += "[Driver] ";
            break;
        case DebugMessageSource::Slang:
            msg += "[Slang] ";
            break;
        default:
            break;
        }
        msg += message;

        auto output = [](const doctest::String& str)
        {
            if (options().verbose)
            {
                MESSAGE(str);
            }
            else
            {
                INFO(str);
            }
        };

        if (type == DebugMessageType::Info)
        {
            output(msg);
        }
        else if (type == DebugMessageType::Warning)
        {
            if (shouldIgnoreError(type, source, message))
            {
                output(msg);
            }
            else
            {
                FAIL(msg);
            }
        }
        else if (type == DebugMessageType::Error)
        {
            if (shouldIgnoreError(type, source, message))
            {
                output(msg);
            }
            else
            {
                FAIL(msg);
            }
        }
    }
};

static DebugCallback sDebugCallback;

void diagnoseIfNeeded(slang::IBlob* diagnosticsBlob)
{
    if (diagnosticsBlob != nullptr)
    {
        MESSAGE(doctest::String((const char*)diagnosticsBlob->getBufferPointer()));
    }
}

Result loadComputeProgram(
    IDevice* device,
    ComPtr<IShaderProgram>& outShaderProgram,
    const char* shaderModuleName,
    const char* entryPointName,
    slang::ProgramLayout*& slangReflection
)
{
    ComPtr<slang::ISession> slangSession;
    SLANG_RETURN_ON_FAIL(device->getSlangSession(slangSession.writeRef()));
    ComPtr<slang::IBlob> diagnosticsBlob;
    slang::IModule* module = slangSession->loadModule(shaderModuleName, diagnosticsBlob.writeRef());
    diagnoseIfNeeded(diagnosticsBlob);
    if (!module)
        return SLANG_FAIL;

    ComPtr<slang::IEntryPoint> computeEntryPoint;
    SLANG_RETURN_ON_FAIL(module->findEntryPointByName(entryPointName, computeEntryPoint.writeRef()));

    std::vector<slang::IComponentType*> componentTypes;
    componentTypes.push_back(module);
    componentTypes.push_back(computeEntryPoint);

    ComPtr<slang::IComponentType> composedProgram;
    Result result = slangSession->createCompositeComponentType(
        componentTypes.data(),
        componentTypes.size(),
        composedProgram.writeRef(),
        diagnosticsBlob.writeRef()
    );
    diagnoseIfNeeded(diagnosticsBlob);
    SLANG_RETURN_ON_FAIL(result);

    ComPtr<slang::IComponentType> linkedProgram;
    result = composedProgram->link(linkedProgram.writeRef(), diagnosticsBlob.writeRef());
    diagnoseIfNeeded(diagnosticsBlob);
    SLANG_RETURN_ON_FAIL(result);

    slangReflection = linkedProgram->getLayout();
    outShaderProgram = device->createShaderProgram(linkedProgram, diagnosticsBlob.writeRef());
    diagnoseIfNeeded(diagnosticsBlob);
    return outShaderProgram ? SLANG_OK : SLANG_FAIL;
}

Result loadComputeProgram(
    IDevice* device,
    slang::ISession* slangSession,
    ComPtr<IShaderProgram>& outShaderProgram,
    const char* shaderModuleName,
    const char* entryPointName,
    slang::ProgramLayout*& slangReflection
)
{
    ComPtr<slang::IBlob> diagnosticsBlob;
    slang::IModule* module = slangSession->loadModule(shaderModuleName, diagnosticsBlob.writeRef());
    diagnoseIfNeeded(diagnosticsBlob);
    if (!module)
        return SLANG_FAIL;

    ComPtr<slang::IEntryPoint> computeEntryPoint;
    SLANG_RETURN_ON_FAIL(module->findEntryPointByName(entryPointName, computeEntryPoint.writeRef()));

    std::vector<slang::IComponentType*> componentTypes;
    componentTypes.push_back(module);
    componentTypes.push_back(computeEntryPoint);

    ComPtr<slang::IComponentType> composedProgram;
    Result result = slangSession->createCompositeComponentType(
        componentTypes.data(),
        componentTypes.size(),
        composedProgram.writeRef(),
        diagnosticsBlob.writeRef()
    );
    diagnoseIfNeeded(diagnosticsBlob);
    SLANG_RETURN_ON_FAIL(result);

    ComPtr<slang::IComponentType> linkedProgram;
    result = composedProgram->link(linkedProgram.writeRef(), diagnosticsBlob.writeRef());
    diagnoseIfNeeded(diagnosticsBlob);
    SLANG_RETURN_ON_FAIL(result);

    slangReflection = linkedProgram->getLayout();
    outShaderProgram = device->createShaderProgram(linkedProgram, diagnosticsBlob.writeRef());
    diagnoseIfNeeded(diagnosticsBlob);
    return outShaderProgram ? SLANG_OK : SLANG_FAIL;
}

Result loadComputeProgramFromSource(IDevice* device, ComPtr<IShaderProgram>& outShaderProgram, std::string_view source)
{
    auto slangSession = device->getSlangSession();
    slang::IModule* module = nullptr;
    ComPtr<slang::IBlob> diagnosticsBlob;
    size_t hash = std::hash<std::string_view>()(source);
    std::string moduleName = "source_module_" + std::to_string(hash);
    auto srcBlob = UnownedBlob::create(source.data(), source.size());
    module =
        slangSession->loadModuleFromSource(moduleName.data(), moduleName.data(), srcBlob, diagnosticsBlob.writeRef());
    diagnoseIfNeeded(diagnosticsBlob);
    if (!module)
        return SLANG_FAIL;

    std::vector<ComPtr<slang::IComponentType>> componentTypes;
    componentTypes.push_back(ComPtr<slang::IComponentType>(module));

    for (SlangInt32 i = 0; i < module->getDefinedEntryPointCount(); i++)
    {
        ComPtr<slang::IEntryPoint> entryPoint;
        SLANG_RETURN_ON_FAIL(module->getDefinedEntryPoint(i, entryPoint.writeRef()));
        componentTypes.push_back(ComPtr<slang::IComponentType>(entryPoint.get()));
    }

    std::vector<slang::IComponentType*> rawComponentTypes;
    for (auto& compType : componentTypes)
        rawComponentTypes.push_back(compType.get());

    ComPtr<slang::IComponentType> linkedProgram;
    Result result = slangSession->createCompositeComponentType(
        rawComponentTypes.data(),
        rawComponentTypes.size(),
        linkedProgram.writeRef(),
        diagnosticsBlob.writeRef()
    );
    diagnoseIfNeeded(diagnosticsBlob);
    SLANG_RETURN_ON_FAIL(result);

    outShaderProgram = device->createShaderProgram(linkedProgram, diagnosticsBlob.writeRef());
    diagnoseIfNeeded(diagnosticsBlob);
    return outShaderProgram ? SLANG_OK : SLANG_FAIL;
}

Result loadGraphicsProgram(
    IDevice* device,
    ComPtr<IShaderProgram>& outShaderProgram,
    const char* shaderModuleName,
    const char* vertexEntryPointName,
    const char* fragmentEntryPointName,
    slang::ProgramLayout*& slangReflection
)
{
    ComPtr<slang::ISession> slangSession;
    SLANG_RETURN_ON_FAIL(device->getSlangSession(slangSession.writeRef()));
    ComPtr<slang::IBlob> diagnosticsBlob;
    slang::IModule* module = slangSession->loadModule(shaderModuleName, diagnosticsBlob.writeRef());
    diagnoseIfNeeded(diagnosticsBlob);
    if (!module)
        return SLANG_FAIL;

    ComPtr<slang::IEntryPoint> vertexEntryPoint;
    SLANG_RETURN_ON_FAIL(module->findEntryPointByName(vertexEntryPointName, vertexEntryPoint.writeRef()));

    ComPtr<slang::IEntryPoint> fragmentEntryPoint;
    SLANG_RETURN_ON_FAIL(module->findEntryPointByName(fragmentEntryPointName, fragmentEntryPoint.writeRef()));

    std::vector<slang::IComponentType*> componentTypes;
    componentTypes.push_back(module);
    componentTypes.push_back(vertexEntryPoint);
    componentTypes.push_back(fragmentEntryPoint);

    ComPtr<slang::IComponentType> composedProgram;
    Result result = slangSession->createCompositeComponentType(
        componentTypes.data(),
        componentTypes.size(),
        composedProgram.writeRef(),
        diagnosticsBlob.writeRef()
    );
    diagnoseIfNeeded(diagnosticsBlob);
    SLANG_RETURN_ON_FAIL(result);

    ComPtr<slang::IComponentType> linkedProgram;
    result = composedProgram->link(linkedProgram.writeRef(), diagnosticsBlob.writeRef());
    diagnoseIfNeeded(diagnosticsBlob);
    SLANG_RETURN_ON_FAIL(result);

    slangReflection = linkedProgram->getLayout();
    outShaderProgram = device->createShaderProgram(linkedProgram, diagnosticsBlob.writeRef());
    diagnoseIfNeeded(diagnosticsBlob);
    return outShaderProgram ? SLANG_OK : SLANG_FAIL;
}

Result loadRenderProgramFromSource(
    IDevice* device,
    ComPtr<IShaderProgram>& outShaderProgram,
    std::string_view source,
    const char* vertexEntryPointName,
    const char* fragmentEntryPointName
)
{
    auto slangSession = device->getSlangSession();
    slang::IModule* module = nullptr;
    ComPtr<slang::IBlob> diagnosticsBlob;
    size_t hash = std::hash<std::string_view>()(source);
    std::string moduleName = "source_module_" + std::to_string(hash);
    auto srcBlob = UnownedBlob::create(source.data(), source.size());
    module =
        slangSession->loadModuleFromSource(moduleName.data(), moduleName.data(), srcBlob, diagnosticsBlob.writeRef());
    diagnoseIfNeeded(diagnosticsBlob);
    if (!module)
        return SLANG_FAIL;

    std::vector<ComPtr<slang::IComponentType>> componentTypes;
    componentTypes.push_back(ComPtr<slang::IComponentType>(module));

    ComPtr<slang::IEntryPoint> vertexEntryPoint;
    SLANG_RETURN_ON_FAIL(module->findEntryPointByName(vertexEntryPointName, vertexEntryPoint.writeRef()));
    componentTypes.push_back(ComPtr<slang::IComponentType>(vertexEntryPoint.get()));

    ComPtr<slang::IEntryPoint> fragmentEntryPoint;
    SLANG_RETURN_ON_FAIL(module->findEntryPointByName(fragmentEntryPointName, fragmentEntryPoint.writeRef()));
    componentTypes.push_back(ComPtr<slang::IComponentType>(fragmentEntryPoint.get()));

    std::vector<slang::IComponentType*> rawComponentTypes;
    for (auto& compType : componentTypes)
        rawComponentTypes.push_back(compType.get());

    ComPtr<slang::IComponentType> linkedProgram;
    Result result = slangSession->createCompositeComponentType(
        rawComponentTypes.data(),
        rawComponentTypes.size(),
        linkedProgram.writeRef(),
        diagnosticsBlob.writeRef()
    );
    diagnoseIfNeeded(diagnosticsBlob);
    SLANG_RETURN_ON_FAIL(result);

    outShaderProgram = device->createShaderProgram(linkedProgram, diagnosticsBlob.writeRef());
    diagnoseIfNeeded(diagnosticsBlob);
    return outShaderProgram ? SLANG_OK : SLANG_FAIL;
}

ComPtr<IDevice> createTestingDevice(
    GpuTestContext* ctx,
    DeviceType deviceType,
    bool useCachedDevice,
    const DeviceExtraOptions* extraOptions
)
{
    // Extra options can only be used when not using cached device.
    if (useCachedDevice)
    {
        REQUIRE(extraOptions == nullptr);
    }

    if (useCachedDevice)
    {
        auto it = gCachedDevices.find(deviceType);
        if (it != gCachedDevices.end())
        {
            return it->second;
        }
    }

    ComPtr<IDevice> device;
    DeviceDesc deviceDesc = {};
    deviceDesc.deviceType = deviceType;
#if ENABLE_SHADER_CACHE
    deviceDesc.persistentShaderCache = &gShaderCache;
#endif

    std::vector<const char*> searchPaths = getSlangSearchPaths();
    if (extraOptions)
    {
        for (const char* path : extraOptions->searchPaths)
            searchPaths.push_back(path);
        if (extraOptions->persistentShaderCache)
            deviceDesc.persistentShaderCache = extraOptions->persistentShaderCache;
        if (extraOptions->persistentPipelineCache)
            deviceDesc.persistentPipelineCache = extraOptions->persistentPipelineCache;
        deviceDesc.enableCompilationReports = extraOptions->enableCompilationReports;
        deviceDesc.existingDeviceHandles = extraOptions->existingDeviceHandles;
    }

    std::vector<slang::PreprocessorMacroDesc> preprocessorMacros;
    std::vector<slang::CompilerOptionEntry> compilerOptions;

    slang::CompilerOptionEntry emitSpirvDirectlyEntry;
    emitSpirvDirectlyEntry.name = slang::CompilerOptionName::EmitSpirvDirectly;
    emitSpirvDirectlyEntry.value.intValue0 = 1;
    compilerOptions.push_back(emitSpirvDirectlyEntry);
#if DEBUG_SPIRV
    slang::CompilerOptionEntry debugLevelCompilerOptionEntry = {};
    debugLevelCompilerOptionEntry.name = slang::CompilerOptionName::DebugInformation;
    debugLevelCompilerOptionEntry.value.intValue0 = SLANG_DEBUG_INFO_LEVEL_STANDARD;
    compilerOptions.push_back(debugLevelCompilerOptionEntry);
#endif
#if DUMP_INTERMEDIATES
    slang::CompilerOptionEntry dumpIntermediatesOptionEntry = {};
    dumpIntermediatesOptionEntry.name = slang::CompilerOptionName::DumpIntermediates;
    dumpIntermediatesOptionEntry.value.intValue0 = 1;
    compilerOptions.push_back(dumpIntermediatesOptionEntry);
#endif

#if SLANG_RHI_ENABLE_NVAPI
    // Setup NVAPI shader extension
#if 0
    // Current NVAPI headers are not compatible with fxc anymore (HitObject API)
    if (deviceType == DeviceType::D3D11)
    {
        deviceDesc.nvapiExtUavSlot = 999;
        preprocessorMacros.push_back({"NV_SHADER_EXTN_SLOT", "u999"});
        slang::CompilerOptionEntry nvapiSearchPath;
        nvapiSearchPath.name = slang::CompilerOptionName::DownstreamArgs;
        nvapiSearchPath.value.kind = slang::CompilerOptionValueKind::String;
        nvapiSearchPath.value.stringValue0 = "fxc";
        nvapiSearchPath.value.stringValue1 = "-I" SLANG_RHI_NVAPI_INCLUDE_DIR;
        compilerOptions.push_back(nvapiSearchPath);
    }
#endif
    if (deviceType == DeviceType::D3D12)
    {
        deviceDesc.nvapiExtUavSlot = 999;
        preprocessorMacros.push_back({"NV_SHADER_EXTN_SLOT", "u999"});
        slang::CompilerOptionEntry nvapiSearchPath = {};
        nvapiSearchPath.name = slang::CompilerOptionName::DownstreamArgs;
        nvapiSearchPath.value.kind = slang::CompilerOptionValueKind::String;
        nvapiSearchPath.value.stringValue0 = "dxc";
        nvapiSearchPath.value.stringValue1 = "-I" SLANG_RHI_NVAPI_INCLUDE_DIR;
        compilerOptions.push_back(nvapiSearchPath);
    }
#endif

#if SLANG_RHI_ENABLE_OPTIX
    // Setup Optix headers
    if (deviceType == DeviceType::CUDA)
    {
        slang::CompilerOptionEntry optixSearchPath;
        optixSearchPath.name = slang::CompilerOptionName::DownstreamArgs;
        optixSearchPath.value.kind = slang::CompilerOptionValueKind::String;
        optixSearchPath.value.stringValue0 = "nvrtc";
        optixSearchPath.value.stringValue1 = "-I" SLANG_RHI_OPTIX_INCLUDE_DIR;
        compilerOptions.push_back(optixSearchPath);
    }
#endif

    deviceDesc.slang.slangGlobalSession = ctx->slangGlobalSession;
    deviceDesc.slang.searchPaths = searchPaths.data();
    deviceDesc.slang.searchPathCount = searchPaths.size();
    deviceDesc.slang.preprocessorMacros = preprocessorMacros.data();
    deviceDesc.slang.preprocessorMacroCount = preprocessorMacros.size();
    deviceDesc.slang.compilerOptionEntries = compilerOptions.data();
    deviceDesc.slang.compilerOptionEntryCount = compilerOptions.size();

    D3D12DeviceExtendedDesc extDesc = {};
    if (deviceType == DeviceType::D3D12)
    {
        extDesc.rootParameterShaderAttributeName = "root";
        deviceDesc.next = &extDesc;
    }

#if SLANG_RHI_DEBUG
    deviceDesc.enableValidation = true;
    deviceDesc.enableRayTracingValidation = true;
    deviceDesc.debugCallback = &sDebugCallback;
#else
    SLANG_UNUSED(sDebugCallback);
#endif

    REQUIRE_CALL(getRHI()->createDevice(deviceDesc, device.writeRef()));

    if (useCachedDevice)
    {
        gCachedDevices[deviceType] = device;
    }

    return device;
}

void releaseCachedDevices()
{
    gCachedDevices.clear();
    getRHI()->reportLiveObjects();
}

const char* getTestsDir()
{
    return SLANG_RHI_TESTS_DIR;
}

std::vector<const char*> getSlangSearchPaths()
{
    return {getTestsDir()};
}

#if ENABLE_RENDERDOC
static RENDERDOC_API_1_6_0* renderdoc_api = nullptr;
void initializeRenderDoc()
{
    if (renderdoc_api)
        return;

    SharedLibraryHandle module = {};
#if SLANG_WINDOWS_FAMILY
    if (!SLANG_SUCCEEDED(loadSharedLibrary("renderdoc.dll", module)))
        return;
#elif SLANG_LINUX_FAMILY
    if (!SLANG_SUCCEEDED(loadSharedLibrary("librenderdoc.so", module)))
        return;
#else
    return;
#endif

    pRENDERDOC_GetAPI RENDERDOC_GetAPI = (pRENDERDOC_GetAPI)findSymbolAddressByName(module, "RENDERDOC_GetAPI");
    int ret = RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_6_0, (void**)&renderdoc_api);
    if (ret != 1 || renderdoc_api == nullptr)
    {
        renderdoc_api = nullptr;
        unloadSharedLibrary(module);
        return;
    }
}

void renderDocBeginFrame()
{
    initializeRenderDoc();
    if (renderdoc_api)
    {
        renderdoc_api->StartFrameCapture(nullptr, nullptr);
    }
}

void renderDocEndFrame()
{
    if (renderdoc_api)
    {
        renderdoc_api->EndFrameCapture(nullptr, nullptr);
    }
}
#else
void initializeRenderDoc() {}
void renderDocBeginFrame() {}
void renderDocEndFrame() {}
#endif

inline const char* deviceTypeToString(DeviceType deviceType)
{
    switch (deviceType)
    {
    case DeviceType::D3D11:
        return "d3d11";
    case DeviceType::D3D12:
        return "d3d12";
    case DeviceType::Vulkan:
        return "vulkan";
    case DeviceType::Metal:
        return "metal";
    case DeviceType::CPU:
        return "cpu";
    case DeviceType::CUDA:
        return "cuda";
    case DeviceType::WGPU:
        return "wgpu";
    default:
        return "unknown";
    }
}

static std::map<DeviceType, bool> sDeviceTypeAvailable;

DeviceAvailabilityResult checkDeviceTypeAvailable(DeviceType deviceType)
{
#define RETURN_NOT_AVAILABLE(msg)                                                                                      \
    {                                                                                                                  \
        result.available = false;                                                                                      \
        result.error = msg;                                                                                            \
        result.debugCallbackOutput = sCaptureDebugCallback.output;                                                     \
        result.diagnostics = diagnostics ? (const char*)diagnostics->getBufferPointer() : "";                          \
        return result;                                                                                                 \
    }

    DeviceAvailabilityResult result;
    result.available = true;

    ComPtr<slang::IBlob> diagnostics;

    sCaptureDebugCallback.clear();

    if (!rhi::getRHI()->isDeviceTypeSupported(deviceType))
        RETURN_NOT_AVAILABLE("backend not supported");

#if SLANG_LINUX_FAMILY
    if (deviceType == DeviceType::CPU)
        // Known issues with CPU backend on linux.
        RETURN_NOT_AVAILABLE("CPU backend not supported on linux");
#endif

    // Try creating a device.
    ComPtr<IDevice> device;
    DeviceDesc desc;
    desc.deviceType = deviceType;
#if SLANG_RHI_DEBUG
    desc.debugCallback = &sCaptureDebugCallback;
#endif

    rhi::Result createResult = rhi::getRHI()->createDevice(desc, device.writeRef());
    if (!SLANG_SUCCEEDED(createResult))
        RETURN_NOT_AVAILABLE("failed to create device");

    // Try compiling a trivial shader.
    ComPtr<slang::ISession> session = device->getSlangSession();
    if (!session)
        RETURN_NOT_AVAILABLE("failed to get slang session");

    // Load shader module.
    slang::IModule* module = nullptr;
    {
        const char* source =
            "[shader(\"compute\")] [numthreads(1,1,1)] void computeMain(uint3 tid : SV_DispatchThreadID) {}";
        diagnostics.setNull();
        module = session->loadModuleFromSourceString("test", "test", source, diagnostics.writeRef());
        if (!module)
            RETURN_NOT_AVAILABLE("failed to shader module");
    }

    ComPtr<slang::IEntryPoint> entryPoint;
    if (!SLANG_SUCCEEDED(module->findEntryPointByName("computeMain", entryPoint.writeRef())))
        RETURN_NOT_AVAILABLE("failed to find shader entry point");

    ComPtr<slang::IComponentType> composedProgram;
    {
        std::vector<slang::IComponentType*> componentTypes;
        componentTypes.push_back(module);
        componentTypes.push_back(entryPoint);
        diagnostics.setNull();
        session->createCompositeComponentType(
            componentTypes.data(),
            componentTypes.size(),
            composedProgram.writeRef(),
            diagnostics.writeRef()
        );
        if (!composedProgram)
            RETURN_NOT_AVAILABLE("failed to create composite component type");
    }

    ComPtr<slang::IComponentType> linkedProgram;
    {
        diagnostics.setNull();
        composedProgram->link(linkedProgram.writeRef(), diagnostics.writeRef());
        if (!linkedProgram)
            RETURN_NOT_AVAILABLE("failed to link shader program");
    }

    if (deviceType == DeviceType::CPU)
    {
        ComPtr<ISlangSharedLibrary> sharedLibrary;
        diagnostics.setNull();
        auto compileResult =
            linkedProgram->getEntryPointHostCallable(0, 0, sharedLibrary.writeRef(), diagnostics.writeRef());
        if (SLANG_FAILED(compileResult))
            RETURN_NOT_AVAILABLE("failed to get entry point host callable");
        auto func = sharedLibrary->findSymbolAddressByName("computeMain");
        if (!func)
            RETURN_NOT_AVAILABLE("failed to find entry point host callable symbol");
    }
    else
    {
        ComPtr<slang::IBlob> code;
        {
            diagnostics.setNull();
            linkedProgram->getEntryPointCode(0, 0, code.writeRef(), diagnostics.writeRef());
            if (!code)
                RETURN_NOT_AVAILABLE("failed to get shader entry point code");
        }
    }

    result.device = device;
    sDeviceTypeAvailable[deviceType] = true;

    return result;
}

bool isDeviceTypeAvailable(DeviceType deviceType)
{
    auto it = sDeviceTypeAvailable.find(deviceType);
    if (it == sDeviceTypeAvailable.end())
    {
        checkDeviceTypeAvailable(deviceType);
    }
    return sDeviceTypeAvailable[deviceType];
}

bool isSwiftShaderDevice(IDevice* device)
{
    std::string adapterName = device->getInfo().adapterName;
    std::transform(
        adapterName.begin(),
        adapterName.end(),
        adapterName.begin(),
        [](unsigned char c) { return std::tolower(c); }
    );
    return adapterName.find("swiftshader") != std::string::npos;
}

slang::IGlobalSession* getSlangGlobalSession()
{
    static slang::IGlobalSession* slangGlobalSession = []()
    {
        slang::IGlobalSession* session = nullptr;
        REQUIRE_CALL(slang::createGlobalSession(&session));
        return session;
    }();
    return slangGlobalSession;
}

void runGpuTestFunc(GpuTestFunc func, int testFlags)
{
    bool createDevice = (testFlags & GpuTestFlags::DontCreateDevice) == 0;
    bool cacheDevice = (testFlags & GpuTestFlags::DontCacheDevice) == 0;

    for (int i = 1; i <= 7; i++)
    {
        if ((testFlags & (1 << i)) == 0)
            continue;

        DeviceType deviceType = DeviceType(i);

        SUBCASE(deviceTypeToString(deviceType))
        {
            if (isDeviceTypeAvailable(deviceType))
            {
                GpuTestContext ctx;
                ctx.deviceType = deviceType;
                ctx.slangGlobalSession = getSlangGlobalSession();
                ComPtr<IDevice> device;
                if (createDevice)
                {
                    device = createTestingDevice(&ctx, deviceType, cacheDevice);
                }
                func(&ctx, device);
            }
        }
    }
}

} // namespace rhi::testing
