#include "testing.h"
#include "shader-cache.h"
#include "core/platform.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>
#include <fstream>

/// Enable dumping intermediate shader files (HLSL, SPIR-V, PTX etc.) to disk for debugging purposes.
#define DUMP_INTERMEDIATES 0

/// Enable device caching.
/// This allows reusing the same device across multiple tests, which can speed up tests significantly.
#define ENABLE_DEVICE_CACHE 1

/// Enable caching of compiled shaders on disk across test runs.
#define ENABLE_SHADER_CACHE 0

#define ENABLE_RENDERDOC 0
#define DEBUG_SPIRV 0

#if ENABLE_RENDERDOC
#include <renderdoc_app.h>
#endif

namespace rhi::testing {

static std::map<DeviceType, ComPtr<IDevice>> gCachedDevices;
static ShaderCache gShaderCache;

// Temp directory to create files for teting in.
static std::filesystem::path gTestTempDirectory;

static Feature getShaderModelFeature(uint32_t shaderModel)
{
    switch (shaderModel)
    {
    case 0x51:
        return Feature::SM_5_1;
    case 0x60:
        return Feature::SM_6_0;
    case 0x61:
        return Feature::SM_6_1;
    case 0x62:
        return Feature::SM_6_2;
    case 0x63:
        return Feature::SM_6_3;
    case 0x64:
        return Feature::SM_6_4;
    case 0x65:
        return Feature::SM_6_5;
    case 0x66:
        return Feature::SM_6_6;
    case 0x67:
        return Feature::SM_6_7;
    case 0x68:
        return Feature::SM_6_8;
    case 0x69:
        return Feature::SM_6_9;
    case 0x6a:
        return Feature::SM_6_10;
    default:
        SLANG_RHI_ASSERT_FAILURE("Unhandled D3D12 shader model");
        return Feature::_Count;
    }
}

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
        output += "[" + std::string(rhi::enumToString(type)) + "] ";
        output += "[" + std::string(rhi::enumToString(source)) + "] ";
        output += message;
        output += "\n";
    }
};

static CaptureDebugCallback sCaptureDebugCallback;

class DebugCallback : public IDebugCallback
{
public:
    bool shouldIgnoreMessage(DebugMessageType type, DebugMessageSource source, const char* message)
    {
        if (type != DebugMessageType::Error)
            return false;

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

        if (shouldIgnoreMessage(type, source, message))
            return;

        doctest::String msg = "[" + doctest::String(enumToString(type)) + "] ";
        msg += "[" + doctest::String(enumToString(source)) + "] ";
        msg += message;

        if (type == DebugMessageType::Info)
        {
            if (options().verbose)
            {
                MESSAGE(msg);
            }
            else
            {
                INFO(msg);
            }
        }
        // `DebugMessageType::Warning` is seperate from `DebugMessageType::Info`
        // Since `INFO()` does not output if `options().verbose == false`
        else if (type == DebugMessageType::Warning)
        {
            MESSAGE(msg);
        }
        else if (type == DebugMessageType::Error)
        {
            FAIL(msg);
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

static Result loadModuleFromSource(slang::ISession* slangSession, std::string_view source, slang::IModule** outModule)
{
    static uint64_t counter = 0;
    size_t hash = std::hash<std::string_view>()(source);
    // TODO: If loading the same module name twice, we sometimes get crashes in Slang.
    // For now we work around this by generating a unique module name for each load,
    // but ideally Slang should fail to reuse a module name if the source is different, instead of crashing.
    // Details in https://github.com/shader-slang/slang/issues/10957.
    std::string moduleName = "source_module_" + std::to_string(hash) + "_" + std::to_string(counter++);
    auto srcBlob = UnownedBlob::create(source.data(), source.size());
    ComPtr<slang::IBlob> diagnosticsBlob;
    *outModule =
        slangSession->loadModuleFromSource(moduleName.data(), moduleName.data(), srcBlob, diagnosticsBlob.writeRef());
    diagnoseIfNeeded(diagnosticsBlob);
    if (!*outModule)
        return SLANG_FAIL;
    return SLANG_OK;
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

    std::vector<ComPtr<slang::IEntryPoint>> entryPoints;
    std::vector<slang::IComponentType*> entryPointComponents;
    for (const char* entryPointName : entryPointNames)
    {
        ComPtr<slang::IEntryPoint> entryPoint;
        SLANG_RETURN_ON_FAIL(module->findEntryPointByName(entryPointName, entryPoint.writeRef()));
        entryPointComponents.push_back(entryPoint.get());
        entryPoints.push_back(entryPoint);
    }

    ShaderProgramDesc shaderProgramDesc = {};
    ComPtr<slang::IComponentType> linkedProgram;

    if (performLinking)
    {
        std::vector<slang::IComponentType*> componentTypes;
        componentTypes.push_back(module);
        for (auto* ep : entryPointComponents)
            componentTypes.push_back(ep);

        ComPtr<slang::IComponentType> composedProgram;
        Result result = slangSession->createCompositeComponentType(
            componentTypes.data(),
            componentTypes.size(),
            composedProgram.writeRef(),
            diagnosticsBlob.writeRef()
        );
        diagnoseIfNeeded(diagnosticsBlob);
        SLANG_RETURN_ON_FAIL(result);

        result = composedProgram->link(linkedProgram.writeRef(), diagnosticsBlob.writeRef());
        diagnoseIfNeeded(diagnosticsBlob);
        SLANG_RETURN_ON_FAIL(result);

        if (outSlangReflection)
            *outSlangReflection = linkedProgram->getLayout();

        shaderProgramDesc.slangGlobalScope = linkedProgram.get();
    }
    else
    {
        shaderProgramDesc.slangGlobalScope = module;
        shaderProgramDesc.slangEntryPoints = entryPointComponents.data();
        shaderProgramDesc.slangEntryPointCount = (uint32_t)entryPointComponents.size();
    }

    Result result = device->createShaderProgram(shaderProgramDesc, outShaderProgram, diagnosticsBlob.writeRef());
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
    SLANG_RETURN_ON_FAIL(loadModuleFromSource(slangSession, source, &module));

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
    ComPtr<slang::IBlob> diagnosticsBlob;
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
    SLANG_RETURN_ON_FAIL(loadModuleFromSource(slangSession, source, &module));

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
    ComPtr<slang::IBlob> diagnosticsBlob;
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

void releaseCachedDevices()
{
    gCachedDevices.clear();
    getRHI()->reportLiveObjects();
}

Result tryToChangeCurrentDebugLayerStateAndOptions(DebugLayerOptions targetDebugLayerOptions)
{
    // Clear all cached devices so that we can change debug layer options
    releaseCachedDevices();

    return getRHI()->setDebugLayerOptions(targetDebugLayerOptions);
}

ComPtr<IDevice> createTestingDevice(
    GpuTestContext* ctx,
    DeviceType deviceType,
    bool useCachedDevice,
    const DeviceExtraOptions* extraOptions
)
{
    useCachedDevice = useCachedDevice && ENABLE_DEVICE_CACHE;

    // Extra options can only be used when not using cached device.
    if (useCachedDevice)
    {
        REQUIRE(extraOptions == nullptr);

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
        deviceDesc.enableAftermath = extraOptions->enableAftermath;
        deviceDesc.enableRayTracingValidation = extraOptions->enableRayTracingValidation;
        deviceDesc.enableValidation = extraOptions->enableValidation;
        deviceDesc.aftermathFlags = extraOptions->aftermathFlags;
    }

#ifdef SLANG_RHI_DEBUG
    deviceDesc.debugCallback = &sDebugCallback;
#endif

    std::vector<slang::PreprocessorMacroDesc> preprocessorMacros;
    std::vector<slang::CompilerOptionEntry> compilerOptions;

    if (extraOptions)
    {
        for (const auto& option : extraOptions->compilerOptions)
            compilerOptions.push_back(option);
    }

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
    if (deviceType == DeviceType::D3D12 && !options().d3d12DisableNVAPI)
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

    // Set SLANG_RHI_TEST_D3D12_NATIVE_HIT_OBJECT if NVAPI is disabled explicitly or unavailable in this build.
#if SLANG_RHI_ENABLE_NVAPI
    const bool useNativeD3D12HitObject = options().d3d12DisableNVAPI;
#else
    const bool useNativeD3D12HitObject = true;
#endif
    if (deviceType == DeviceType::D3D12 && useNativeD3D12HitObject)
    {
        preprocessorMacros.push_back({"SLANG_RHI_TEST_D3D12_NATIVE_HIT_OBJECT", "1"});
    }

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

    auto disableWarning = [](const char* warningCode) -> slang::CompilerOptionEntry
    {
        slang::CompilerOptionEntry entry;
        entry.name = slang::CompilerOptionName::DisableWarning;
        entry.value.kind = slang::CompilerOptionValueKind::String;
        entry.value.stringValue0 = warningCode;
        return entry;
    };

    // Disable noisy warnings 31106 and 31107 until slang fixes them.
    // https://github.com/shader-slang/slang/issues/11825
    compilerOptions.push_back(disableWarning("31106"));
    compilerOptions.push_back(disableWarning("31107"));

    deviceDesc.slang.slangGlobalSession = ctx->slangGlobalSession;
    deviceDesc.slang.searchPaths = searchPaths.data();
    deviceDesc.slang.searchPathCount = searchPaths.size();
    deviceDesc.slang.preprocessorMacros = preprocessorMacros.data();
    deviceDesc.slang.preprocessorMacroCount = preprocessorMacros.size();
    deviceDesc.slang.compilerOptionEntries = compilerOptions.data();
    deviceDesc.slang.compilerOptionEntryCount = compilerOptions.size();

    D3D12DeviceExtendedDesc extDesc = {};
    bool requireSpecificD3D12ShaderModel = false;
    if (deviceType == DeviceType::D3D12)
    {
        extDesc.rootParameterShaderAttributeName = "root";
        if (extraOptions && extraOptions->d3d12HighestShaderModel != 0)
        {
            extDesc.highestShaderModel = extraOptions->d3d12HighestShaderModel;
        }
        else if (options().d3d12ShaderModel != 0)
        {
            extDesc.highestShaderModel = options().d3d12ShaderModel;
            requireSpecificD3D12ShaderModel = true;
        }
        else
        {
            // TODO: Slang current emits invalid HitObject code when D3D12 SM 6.9 and NVAPI are enabled.
            // https://github.com/shader-slang/slang/issues/11903
            // We currently default testing to cap at SM 6.8 to avoid this issue,
            // but ideally we should be able to test SM 6.9 with NVAPI enabled.
            // We can test SM 6.9 with/without NVAPI using -d3d12-shader-model and -d3d12-disable-nvapi cli options.
            extDesc.highestShaderModel = 0x68;
        }
        deviceDesc.next = &extDesc;
    }

#if SLANG_RHI_DEBUG
    // We do not set the DebugLayerOptions here since this is done
    // higher up in the call-tree before creating devices.
    // We will set the per device option `enableValidation` here though.
    deviceDesc.enableValidation = true;
#endif

    REQUIRE_CALL(getRHI()->createDevice(deviceDesc, device.writeRef()));

    if (requireSpecificD3D12ShaderModel)
    {
        Feature feature = getShaderModelFeature(extDesc.highestShaderModel);
        REQUIRE(device->hasFeature(feature));
    }

    if (useCachedDevice)
    {
        gCachedDevices[deviceType] = device;
    }

    return device;
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
    D3D12DeviceExtendedDesc d3d12ExtDesc = {};
    if (deviceType == DeviceType::D3D12 && options().d3d12ShaderModel != 0)
    {
        d3d12ExtDesc.highestShaderModel = options().d3d12ShaderModel;
        desc.next = &d3d12ExtDesc;
    }
#if SLANG_RHI_ENABLE_NVAPI
    if (deviceType == DeviceType::D3D12 && !options().d3d12DisableNVAPI)
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

    // Set CUDA context current (no-op for non-CUDA devices).
    device->setCudaContextCurrent();

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

static std::map<DeviceType, int> sGpuTestsEncountered;
static std::map<DeviceType, int> sGpuTestsExecuted;

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

    sGpuTestsEncountered[deviceType]++;

    if (!isDeviceTypeSelected(deviceType))
    {
        SKIP("device not selected");
    }

    if (isDeviceTypeAvailable(deviceType))
    {
        static bool cachedPreviousDebugLayer = false;
        static DebugLayerOptions previousDebugLayerOptions;

        // Switch back to the old `DebugLayerOptions`.
        // We need to cache the previous state via a static
        // since if an assert is hit, we want our code to be
        // aware that "we still did not switch back to the old
        // debug settings".
        if (cachedPreviousDebugLayer)
        {
            REQUIRE_CALL(tryToChangeCurrentDebugLayerStateAndOptions(previousDebugLayerOptions));
            cachedPreviousDebugLayer = false;
        }

        // Cache the default `DebugLayerOptions` and switch if
        // the test requests different `DebugLayerOptions`.
        previousDebugLayerOptions = getRHI()->getDebugLayerOptions();
        bool testRequestsDifferentDebugLayerOptions =
            info->hasDebugLayerOptions && (previousDebugLayerOptions != info->debugLayerOptions);
        if (testRequestsDifferentDebugLayerOptions)
        {
            REQUIRE_CALL(tryToChangeCurrentDebugLayerStateAndOptions(info->debugLayerOptions));
            cachedPreviousDebugLayer = true;
        }

        // Run test
        GpuTestContext ctx;
        ctx.deviceType = deviceType;
        ctx.slangGlobalSession = getSlangGlobalSession();
        ComPtr<IDevice> device;
        if (createDevice)
        {
            device = createTestingDevice(&ctx, deviceType, cacheDevice);
        }
        // Set CUDA context current before running tests (no-op for non-CUDA devices).
        // This simulates how SlangPy will call it at entry points like Function.call().
        if (device)
        {
            device->setCudaContextCurrent();
        }
        info->func(&ctx, device);
        reportGpuTestExecuted(deviceType);
        if (device)
        {
            device->getQueue(QueueType::Graphics)->waitOnHost();
            device.setNull();
        }
        if (!ENABLE_DEVICE_CACHE)
        {
            getRHI()->reportLiveObjects();
        }
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
int registerGpuTest(
    const char* name,
    GpuTestFunc func,
    GpuTestFlags flags,
    std::optional<DebugLayerOptions> debugLayerOptions,
    const char* file,
    int line
)
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
        info->hasDebugLayerOptions = debugLayerOptions.has_value();
        info->debugLayerOptions = debugLayerOptions.value_or(DebugLayerOptions{});

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

void reportGpuTestExecuted(DeviceType deviceType)
{
    sGpuTestsExecuted[deviceType]++;
}

bool checkNoSilentGpuSkips()
{
    bool ok = true;
    for (DeviceType deviceType : kPlatformDeviceTypes)
    {
        if (!isDeviceTypeSelected(deviceType))
            continue;
        if (sGpuTestsEncountered.find(deviceType) == sGpuTestsEncountered.end())
            continue;
        auto availIt = sDeviceTypeAvailable.find(deviceType);
        if (availIt == sDeviceTypeAvailable.end() || !availIt->second)
            continue;
        auto execIt = sGpuTestsExecuted.find(deviceType);
        int count = (execIt != sGpuTestsExecuted.end()) ? execIt->second : 0;
        if (count == 0)
        {
            std::fprintf(
                stderr,
                "ERROR: Device type '%s' was available but zero tests executed "
                "(all silently skipped). This likely indicates a device "
                "initialization regression.\n",
                deviceTypeToString(deviceType)
            );
            ok = false;
        }
    }
    return ok;
}

} // namespace rhi::testing
