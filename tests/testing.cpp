#include "testing.h"
#include "shader-cache.h"
#include "core/platform.h"
#include <algorithm>
#include <cctype>
#include <ctime>
#include <cstdlib>
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

const char* getEnvVariable(const char* name, const char* defaultValue = nullptr)
{
#if SLANG_WINDOWS_FAMILY
    static char value[4096];
    size_t len = 0;
    if (::getenv_s(&len, value, sizeof(value), "SLANG_RHI_TESTS_DIR") == 0 && len > 0)
        return static_cast<const char*>(value);
    else
        return defaultValue;
#else
    const char* value = ::getenv(name);
    return value ? value : defaultValue;
#endif
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
            output(msg);
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


static Result loadProgram(
    IDevice* device,
    slang::ISession* slangSession,
    const char* shaderModuleName,
    std::vector<const char*> entryPointNames,
    bool performLinking,
    IShaderProgram** outShaderProgram,
    slang::ProgramLayout** outSlangReflection
)
{
    ComPtr<slang::ISession> ownedSlangSession;
    if (!slangSession)
    {
        SLANG_RETURN_ON_FAIL(device->getSlangSession(ownedSlangSession.writeRef()));
        slangSession = ownedSlangSession.get();
    }

    ComPtr<slang::IBlob> diagnosticsBlob;
    slang::IModule* module = slangSession->loadModule(shaderModuleName, diagnosticsBlob.writeRef());
    diagnoseIfNeeded(diagnosticsBlob);
    if (!module)
        return SLANG_FAIL;

    std::vector<slang::IComponentType*> componentTypes;
    componentTypes.push_back(module);

    // Find all entry points
    for (const char* entryPointName : entryPointNames)
    {
        ComPtr<slang::IEntryPoint> entryPoint;
        SLANG_RETURN_ON_FAIL(module->findEntryPointByName(entryPointName, entryPoint.writeRef()));
        componentTypes.push_back(entryPoint);
    }

    // Create composite component type
    ComPtr<slang::IComponentType> composedProgram;
    Result result = slangSession->createCompositeComponentType(
        componentTypes.data(),
        componentTypes.size(),
        composedProgram.writeRef(),
        diagnosticsBlob.writeRef()
    );
    diagnoseIfNeeded(diagnosticsBlob);
    SLANG_RETURN_ON_FAIL(result);

    slang::IComponentType* programToUse = composedProgram.get();
    ComPtr<slang::IComponentType> linkedProgram;

    if (performLinking)
    {
        result = composedProgram->link(linkedProgram.writeRef(), diagnosticsBlob.writeRef());
        diagnoseIfNeeded(diagnosticsBlob);
        SLANG_RETURN_ON_FAIL(result);

        programToUse = linkedProgram.get();
        if (outSlangReflection)
            *outSlangReflection = linkedProgram->getLayout();
    }

    ShaderProgramDesc shaderProgramDesc = {};
    shaderProgramDesc.slangGlobalScope = programToUse;
    result = device->createShaderProgram(shaderProgramDesc, outShaderProgram, diagnosticsBlob.writeRef());
    diagnoseIfNeeded(diagnosticsBlob);
    return result;
}

Result loadProgram(
    IDevice* device,
    slang::ISession* slangSession,
    const char* shaderModuleName,
    std::vector<const char*> entryPointNames,
    IShaderProgram** outShaderProgram
)
{
    return loadProgram(device, slangSession, shaderModuleName, entryPointNames, false, outShaderProgram, nullptr);
}

Result loadProgram(
    IDevice* device,
    slang::ISession* slangSession,
    const char* shaderModuleName,
    const char* entryPointName,
    IShaderProgram** outShaderProgram
)
{
    return loadProgram(
        device,
        slangSession,
        shaderModuleName,
        std::vector<const char*>{entryPointName},
        outShaderProgram
    );
}

Result loadProgram(
    IDevice* device,
    const char* shaderModuleName,
    std::vector<const char*> entryPointNames,
    IShaderProgram** outShaderProgram
)
{
    return loadProgram(device, nullptr, shaderModuleName, entryPointNames, false, outShaderProgram, nullptr);
}

Result loadProgram(
    IDevice* device,
    const char* shaderModuleName,
    const char* entryPointName,
    IShaderProgram** outShaderProgram
)
{
    return loadProgram(device, shaderModuleName, std::vector<const char*>{entryPointName}, outShaderProgram);
}

Result loadAndLinkProgram(
    IDevice* device,
    slang::ISession* slangSession,
    const char* shaderModuleName,
    std::vector<const char*> entryPointNames,
    IShaderProgram** outShaderProgram,
    slang::ProgramLayout** outSlangReflection
)
{
    return loadProgram(
        device,
        slangSession,
        shaderModuleName,
        entryPointNames,
        true,
        outShaderProgram,
        outSlangReflection
    );
}

Result loadAndLinkProgram(
    IDevice* device,
    slang::ISession* slangSession,
    const char* shaderModuleName,
    const char* entryPointName,
    IShaderProgram** outShaderProgram,
    slang::ProgramLayout** outSlangReflection
)
{
    return loadAndLinkProgram(
        device,
        slangSession,
        shaderModuleName,
        std::vector<const char*>{entryPointName},
        outShaderProgram,
        outSlangReflection
    );
}

Result loadAndLinkProgram(
    IDevice* device,
    const char* shaderModuleName,
    std::vector<const char*> entryPointNames,
    IShaderProgram** outShaderProgram,
    slang::ProgramLayout** outSlangReflection
)
{
    return loadProgram(device, nullptr, shaderModuleName, entryPointNames, true, outShaderProgram, outSlangReflection);
}

Result loadAndLinkProgram(
    IDevice* device,
    const char* shaderModuleName,
    const char* entryPointName,
    IShaderProgram** outShaderProgram,
    slang::ProgramLayout** outSlangReflection
)
{
    return loadAndLinkProgram(
        device,
        shaderModuleName,
        std::vector<const char*>{entryPointName},
        outShaderProgram,
        outSlangReflection
    );
}

Result loadComputeProgramFromSource(IDevice* device, std::string_view source, IShaderProgram** outShaderProgram)
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

    ShaderProgramDesc shaderProgramDesc = {};
    shaderProgramDesc.slangGlobalScope = linkedProgram;
    result = device->createShaderProgram(shaderProgramDesc, outShaderProgram, diagnosticsBlob.writeRef());
    diagnoseIfNeeded(diagnosticsBlob);
    return result;
}

Result loadRenderProgramFromSource(
    IDevice* device,
    std::string_view source,
    const char* vertexEntryPointName,
    const char* fragmentEntryPointName,
    IShaderProgram** outShaderProgram
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

    ShaderProgramDesc shaderProgramDesc = {};
    shaderProgramDesc.slangGlobalScope = linkedProgram;
    result = device->createShaderProgram(shaderProgramDesc, outShaderProgram, diagnosticsBlob.writeRef());
    diagnoseIfNeeded(diagnosticsBlob);
    return result;
}

const char* deviceTypeToString(DeviceType deviceType)
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
    deviceDesc.adapter = getSelectedDeviceAdapter(deviceType);
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
    // Setup OptiX headers
    std::string optixIncludeStr;
    if (deviceType == DeviceType::CUDA)
    {
        deviceDesc.requiredOptixVersion = options().optixVersion;
        slang::CompilerOptionEntry optixSearchPath;
        optixSearchPath.name = slang::CompilerOptionName::DownstreamArgs;
        optixSearchPath.value.kind = slang::CompilerOptionValueKind::String;
        optixSearchPath.value.stringValue0 = "nvrtc";

        // Try to locate OptiX headers from the following locations:
        // - SLANG_RHI_OPTIX_DEVICE_HEADER_INCLUDE_DIR (set at cmake configure time)
        // - <exe path>/optix (where exe path is the directory containing the test executable)
        // - ./optix (current working directory)
        auto findOptixDir = []() -> std::filesystem::path
        {
            std::vector<std::filesystem::path> candidatePaths{
                SLANG_RHI_OPTIX_DEVICE_HEADER_INCLUDE_DIR,
                std::filesystem::path(exePath()).parent_path() / "optix",
                std::filesystem::current_path() / "optix",
            };
            for (const auto& path : candidatePaths)
                if (std::filesystem::exists(path / "9_0" / "optix.h"))
                    return path;
            return {};
        };

        std::filesystem::path optixDir = findOptixDir();
        if (optixDir.empty())
        {
            FAIL("OptiX headers not found");
        }

        if (deviceDesc.requiredOptixVersion == 0 || deviceDesc.requiredOptixVersion == 90000)
        {
            optixIncludeStr = "-I" + (optixDir / "9_0").string();
        }
        else if (deviceDesc.requiredOptixVersion == 80100)
        {
            optixIncludeStr = "-I" + (optixDir / "8_1").string();
        }
        else if (deviceDesc.requiredOptixVersion == 80000)
        {
            optixIncludeStr = "-I" + (optixDir / "8_0").string();
        }
        else
        {
            FAIL("Unsupported OptiX version");
        }
        optixSearchPath.value.stringValue1 = optixIncludeStr.c_str();
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
        if (extraOptions && extraOptions->d3d12HighestShaderModel != 0)
        {
            extDesc.highestShaderModel = extraOptions->d3d12HighestShaderModel;
        }
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
    const char* value = getEnvVariable("SLANG_RHI_TESTS_DIR");
    return (value && value[0] != '\0') ? value : SLANG_RHI_TESTS_DIR;
}

std::vector<const char*> getSlangSearchPaths()
{
    return std::vector<const char*>{
        getTestsDir(),
    };
}

#if ENABLE_RENDERDOC
static RENDERDOC_API_1_6_0* renderdoc_api = nullptr;
void initializeRenderDoc()
{
    if (renderdoc_api)
        return;

    SharedLibraryHandle module = {};
#if SLANG_WINDOWS_FAMILY
    if (SLANG_FAILED(loadSharedLibrary("renderdoc.dll", module)))
        return;
#elif SLANG_LINUX_FAMILY
    if (SLANG_FAILED(loadSharedLibrary("librenderdoc.so", module)))
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
    desc.adapter = getSelectedDeviceAdapter(deviceType);
#if SLANG_RHI_DEBUG
    desc.debugCallback = &sCaptureDebugCallback;
#endif
#if SLANG_RHI_ENABLE_NVAPI
    if (deviceType == DeviceType::D3D12)
    {
        desc.nvapiExtUavSlot = 999;
    }
#endif
#if SLANG_RHI_ENABLE_OPTIX
    if (deviceType == DeviceType::CUDA)
    {
        desc.requiredOptixVersion = options().optixVersion;
    }
#endif

    rhi::Result createResult = rhi::getRHI()->createDevice(desc, device.writeRef());
    if (SLANG_FAILED(createResult))
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
    if (SLANG_FAILED(module->findEntryPointByName("computeMain", entryPoint.writeRef())))
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

bool isDeviceTypeSelected(DeviceType deviceType)
{
    return options().deviceSelected[size_t(deviceType)];
}

rhi::IAdapter* getSelectedDeviceAdapter(DeviceType deviceType)
{
    int adapterIndex = options().deviceAdapterIndex[size_t(deviceType)];
    if (adapterIndex < 0)
        return nullptr;
    return rhi::getRHI()->getAdapter(deviceType, adapterIndex);
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

// Trampoline test function registered in doctest for each GPU test instance.
// Uses GpuTestInfo for additional information about the specific test instance.
static void gpuTestTrampoline()
{
    const doctest::TestCaseData* tc = doctest::getContextOptions()->currentTest;
    // GpuTestInfo is stored in front of the test name.
    const GpuTestInfo* info = reinterpret_cast<const GpuTestInfo*>(tc->m_name) - 1;

    DeviceType deviceType = info->deviceType;
    bool createDevice = (info->flags & GpuTestFlags::DontCreateDevice) == 0;
    bool cacheDevice = (info->flags & GpuTestFlags::DontCacheDevice) == 0;

    if (!isDeviceTypeSelected(deviceType))
    {
        SKIP("device not selected");
    }

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
        info->func(&ctx, device);
    }
    else
    {
        SKIP("device not available");
    }
}

// Simple allocator for storing GpuTestInfo and test names.
class GpuTestAllocator
{
public:
    GpuTestAllocator(size_t size = 4 * 1024 * 1024)
        : m_size(size)
    {
        m_data = reinterpret_cast<uint8_t*>(malloc(size));
    }
    ~GpuTestAllocator() { free(m_data); }
    void* allocate(size_t size)
    {
        // Align size to 16 bytes
        size = (size + 15) & ~15;
        if (m_pos + size > m_size)
        {
            SLANG_RHI_ASSERT_FAILURE("Out of memory! Increase the allocation size.");
        }
        void* ptr = m_data + m_pos;
        m_pos += size;
        return ptr;
    }

private:
    size_t m_size;
    uint8_t* m_data;
    size_t m_pos;
};

// Register a GPU test.
// This is called by the GPU_TEST_CASE macro to register a GPU test.
// We do some hackery to register multiple test cases with doctest, one for each device type specified in the flags.
// Because doctest doesn't support any user data in the test case definition and we don't want to alter the
// doctest implementation, we store the GpuTestInfo structure in front of the unique test name used for each
// test instance.
int registerGpuTest(const char* name, GpuTestFunc func, GpuTestFlags flags, const char* file, int line)
{
    static GpuTestAllocator allocator;

    for (int i = 1; i <= 7; i++)
    {
        if ((flags & (1 << i)) == 0)
            continue;

        DeviceType deviceType = DeviceType(i);

        if (!isPlatformDeviceType(deviceType))
            continue;

        size_t testNameLen = strlen(name) + 16;

        GpuTestInfo* info = static_cast<GpuTestInfo*>(allocator.allocate(sizeof(GpuTestInfo) + testNameLen));
        info->func = func;
        info->deviceType = deviceType;
        info->flags = flags;

        char* testName = reinterpret_cast<char*>(info + 1);
        snprintf(testName, testNameLen, "%s.%s", name, deviceTypeToString(deviceType));
        testName[testNameLen - 1] = '\0';

        doctest::detail::regTest(
            doctest::detail::TestCase(
                gpuTestTrampoline,
                file,
                line,
                doctest_detail_test_suite_ns::getCurrentTestSuite()
            ) *
            static_cast<const char*>(testName)
        );
    }

    return 0;
}

static std::map<const doctest::TestCaseData*, const char*> sSkipMessages;

void reportSkip(const doctest::detail::TestCase* tc, const char* reason)
{
    sSkipMessages[tc] = reason;
}

const char* getSkipMessage(const doctest::TestCaseData* tc)
{
    auto it = sSkipMessages.find(tc);
    return it != sSkipMessages.end() ? it->second : nullptr;
}

} // namespace rhi::testing
