#include "testing.h"
#include "shader-cache.h"

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

class DebugCallback : public IDebugCallback
{
public:
    virtual SLANG_NO_THROW void SLANG_MCALL
    handleMessage(DebugMessageType type, DebugMessageSource source, const char* message) override
    {
        if (!doctest::is_running_in_test)
            return;
        MESSAGE(doctest::String(message));
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
    outShaderProgram = device->createShaderProgram(linkedProgram);
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
    outShaderProgram = device->createShaderProgram(linkedProgram);
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
    SLANG_RETURN_ON_FAIL(result);

    outShaderProgram = device->createShaderProgram(linkedProgram);
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
    outShaderProgram = device->createShaderProgram(linkedProgram);
    return outShaderProgram ? SLANG_OK : SLANG_FAIL;
}

void compareComputeResult(
    IDevice* device,
    ITexture* texture,
    void* expectedResult,
    size_t expectedResultRowPitch,
    size_t rowCount
)
{
    // Read back the results.
    ComPtr<ISlangBlob> resultBlob;
    size_t rowPitch = 0;
    size_t pixelSize = 0;
    REQUIRE_CALL(device->readTexture(texture, resultBlob.writeRef(), &rowPitch, &pixelSize));
    // Compare results.
    for (size_t row = 0; row < rowCount; row++)
    {
        CHECK(
            memcmp(
                (uint8_t*)resultBlob->getBufferPointer() + rowPitch * row,
                (uint8_t*)expectedResult + expectedResultRowPitch * row,
                expectedResultRowPitch
            ) == 0
        );
    }
}

ComPtr<IDevice> createTestingDevice(
    GpuTestContext* ctx,
    DeviceType deviceType,
    bool useCachedDevice,
    std::vector<const char*> additionalSearchPaths
)
{
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
    for (const char* path : additionalSearchPaths)
        searchPaths.push_back(path);

    std::vector<slang::PreprocessorMacroDesc> preprocessorMacros;
    std::vector<slang::CompilerOptionEntry> compilerOptions;

    slang::CompilerOptionEntry emitSpirvDirectlyEntry;
    emitSpirvDirectlyEntry.name = slang::CompilerOptionName::EmitSpirvDirectly;
    emitSpirvDirectlyEntry.value.intValue0 = 1;
    compilerOptions.push_back(emitSpirvDirectlyEntry);
#if DEBUG_SPIRV
    slang::CompilerOptionEntry debugLevelCompilerOptionEntry;
    debugLevelCompilerOptionEntry.name = slang::CompilerOptionName::DebugInformation;
    debugLevelCompilerOptionEntry.value.intValue0 = SLANG_DEBUG_INFO_LEVEL_STANDARD;
    compilerOptions.push_back(debugLevelCompilerOptionEntry);
#endif
#if DUMP_INTERMEDIATES
    slang::CompilerOptionEntry dumpIntermediatesOptionEntry;
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
        slang::CompilerOptionEntry nvapiSearchPath;
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

#ifdef _DEBUG
    deviceDesc.enableValidation = true;
    deviceDesc.enableRayTracingValidation = true;
    deviceDesc.debugCallback = &sDebugCallback;
#endif

    REQUIRE_CALL(getRHI()->createDevice(deviceDesc, device.writeRef()));

#ifdef _DEBUG
    const char* features[128];
    uint32_t featureCount;
    REQUIRE_CALL(device->getFeatures(features, SLANG_COUNT_OF(features), &featureCount));
    std::string featureStr;
    for (uint32_t i = 0; i < featureCount; i++)
    {
        featureStr += features[i];
        if (i < featureCount - 1)
            featureStr += " ";
    }
    MESSAGE("Device features: ", featureStr);
#endif

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
RENDERDOC_API_1_1_2* rdoc_api = NULL;
void initializeRenderDoc()
{
    if (HMODULE mod = GetModuleHandleA("renderdoc.dll"))
    {
        pRENDERDOC_GetAPI RENDERDOC_GetAPI = (pRENDERDOC_GetAPI)GetProcAddress(mod, "RENDERDOC_GetAPI");
        int ret = RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_1_2, (void**)&rdoc_api);
        SLANG_RHI_ASSERT(ret == 1);
    }
}
void renderDocBeginFrame()
{
    if (!rdoc_api)
        initializeRenderDoc();
    if (rdoc_api)
        rdoc_api->StartFrameCapture(nullptr, nullptr);
}
void renderDocEndFrame()
{
    if (rdoc_api)
        rdoc_api->EndFrameCapture(nullptr, nullptr);
    _fgetchar();
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

inline bool checkDeviceTypeAvailable(DeviceType deviceType, bool verbose = true)
{
#define RETURN_NOT_AVAILABLE(msg)                                                                                      \
    {                                                                                                                  \
        if (verbose)                                                                                                   \
            MESSAGE(                                                                                                   \
                doctest::String(deviceTypeToString(deviceType)),                                                       \
                " is not available (",                                                                                 \
                doctest::String(msg),                                                                                  \
                ")"                                                                                                    \
            );                                                                                                         \
        return false;                                                                                                  \
    }

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
    if (!SLANG_SUCCEEDED(rhi::getRHI()->createDevice(desc, device.writeRef())))
        RETURN_NOT_AVAILABLE("failed to create device");

    // Try compiling a trivial shader.
    ComPtr<slang::ISession> session = device->getSlangSession();
    if (!session)
        RETURN_NOT_AVAILABLE("failed to get slang session");

    // Load shader module.
    slang::IModule* module = nullptr;
    {
        ComPtr<slang::IBlob> diagnostics;
        const char* source =
            "[shader(\"compute\")] [numthreads(1,1,1)] void computeMain(uint3 tid : SV_DispatchThreadID) {}";
        module = session->loadModuleFromSourceString("test", "test", source, diagnostics.writeRef());
        if (verbose && diagnostics)
            MESSAGE(doctest::String((const char*)diagnostics->getBufferPointer()));
        if (!module)
            RETURN_NOT_AVAILABLE("failed to load module");
    }

    ComPtr<slang::IEntryPoint> entryPoint;
    if (!SLANG_SUCCEEDED(module->findEntryPointByName("computeMain", entryPoint.writeRef())))
        RETURN_NOT_AVAILABLE("failed to find entry point");

    ComPtr<slang::IComponentType> composedProgram;
    {
        ComPtr<slang::IBlob> diagnostics;
        std::vector<slang::IComponentType*> componentTypes;
        componentTypes.push_back(module);
        componentTypes.push_back(entryPoint);
        session->createCompositeComponentType(
            componentTypes.data(),
            componentTypes.size(),
            composedProgram.writeRef(),
            diagnostics.writeRef()
        );
        if (verbose && diagnostics)
            MESSAGE(doctest::String((const char*)diagnostics->getBufferPointer()));
        if (!composedProgram)
            RETURN_NOT_AVAILABLE("failed to create composite component type");
    }

    ComPtr<slang::IComponentType> linkedProgram;
    {
        ComPtr<slang::IBlob> diagnostics;
        composedProgram->link(linkedProgram.writeRef(), diagnostics.writeRef());
        if (verbose && diagnostics)
            MESSAGE(doctest::String((const char*)diagnostics->getBufferPointer()));
        if (!linkedProgram)
            RETURN_NOT_AVAILABLE("failed to link program");
    }

    if (deviceType == DeviceType::CPU)
    {
        ComPtr<ISlangSharedLibrary> sharedLibrary;
        ComPtr<ISlangBlob> diagnostics;
        auto compileResult =
            linkedProgram->getEntryPointHostCallable(0, 0, sharedLibrary.writeRef(), diagnostics.writeRef());
        if (verbose && diagnostics)
            MESSAGE(doctest::String((const char*)diagnostics->getBufferPointer()));
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
            ComPtr<slang::IBlob> diagnostics;
            linkedProgram->getEntryPointCode(0, 0, code.writeRef(), diagnostics.writeRef());
            if (verbose && diagnostics)
                MESSAGE(doctest::String((const char*)diagnostics->getBufferPointer()));
            if (!code)
                RETURN_NOT_AVAILABLE("failed to get entry point code");
        }
    }

    return true;
}

bool isDeviceTypeAvailable(DeviceType deviceType)
{
    static std::map<DeviceType, bool> available;
    auto it = available.find(deviceType);
    if (it == available.end())
    {
        available[deviceType] = checkDeviceTypeAvailable(deviceType);
    }
    return available[deviceType];
}

bool isSwiftShaderDevice(IDevice* device)
{
    std::string adapterName = device->getDeviceInfo().adapterName;
    std::transform(
        adapterName.begin(),
        adapterName.end(),
        adapterName.begin(),
        [](unsigned char c) { return std::tolower(c); }
    );
    return adapterName.find("swiftshader") != std::string::npos;
}

static slang::IGlobalSession* getSlangGlobalSession()
{
    static slang::IGlobalSession* slangGlobalSession = []()
    {
        slang::IGlobalSession* session;
        REQUIRE_CALL(slang::createGlobalSession(&session));
        return session;
    }();
    return slangGlobalSession;
}

void runGpuTests(GpuTestFunc func, std::initializer_list<DeviceType> deviceTypes)
{
    for (auto deviceType : deviceTypes)
    {
        SUBCASE(deviceTypeToString(deviceType))
        {
            if (isDeviceTypeAvailable(deviceType))
            {
                GpuTestContext ctx;
                ctx.slangGlobalSession = getSlangGlobalSession();
                func(&ctx, deviceType);
            }
        }
    }
}

void runGpuTestFunc(void (*func)(IDevice* device), int testFlags)
{
    bool useCachedDevice = (testFlags & TestFlags::NoDeviceCache) == 0;

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
                ctx.slangGlobalSession = getSlangGlobalSession();
                ComPtr<IDevice> device = createTestingDevice(&ctx, deviceType, useCachedDevice);
                func(device);
            }
        }
    }
}

} // namespace rhi::testing
