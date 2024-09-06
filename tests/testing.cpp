#include "testing.h"
#include "shader-cache.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <map>
#include <string>

#define SLANG_RHI_ENABLE_RENDERDOC 0
#define SLANG_RHI_DEBUG_SPIRV 0

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

class DebugCallback : public IDebugCallback
{
public:
    virtual SLANG_NO_THROW void SLANG_MCALL
    handleMessage(DebugMessageType type, DebugMessageSource source, const char* message) override
    {
        switch (type)
        {
        case DebugMessageType::Info:
        {
            INFO("Validation info: ", doctest::String(message));
            break;
        }
        case DebugMessageType::Warning:
        {
            MESSAGE("Validation warning: ", doctest::String(message));
            break;
        }
        case DebugMessageType::Error:
        {
            FAIL("Validation error: ", doctest::String(message));
            break;
        }
        }
    }
};

static DebugCallback sDebugCallback;

void diagnoseIfNeeded(slang::IBlob* diagnosticsBlob)
{
    if (diagnosticsBlob != nullptr)
    {
        MESSAGE((const char*)diagnosticsBlob->getBufferPointer());
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
    ResourceState state,
    void* expectedResult,
    size_t expectedResultRowPitch,
    size_t rowCount
)
{
    // Read back the results.
    ComPtr<ISlangBlob> resultBlob;
    size_t rowPitch = 0;
    size_t pixelSize = 0;
    REQUIRE_CALL(device->readTexture(texture, state, resultBlob.writeRef(), &rowPitch, &pixelSize));
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

void compareComputeResult(
    IDevice* device,
    IBuffer* buffer,
    size_t offset,
    const void* expectedResult,
    size_t expectedBufferSize
)
{
    // Read back the results.
    ComPtr<ISlangBlob> resultBlob;
    REQUIRE_CALL(device->readBuffer(buffer, offset, expectedBufferSize, resultBlob.writeRef()));
    CHECK_EQ(resultBlob->getBufferSize(), expectedBufferSize);
    // Compare results.
    CHECK(memcmp(resultBlob->getBufferPointer(), (uint8_t*)expectedResult, expectedBufferSize) == 0);
}

void compareComputeResultFuzzy(const float* result, float* expectedResult, size_t expectedBufferSize)
{
    for (size_t i = 0; i < expectedBufferSize / sizeof(float); ++i)
    {
        CHECK_LE(result[i], expectedResult[i] + 0.01f);
        CHECK_GE(result[i], expectedResult[i] - 0.01f);
        // CHECK_LE(abs(result[i] - expectedResult[i]), 0.01);
    }
}

void compareComputeResultFuzzy(IDevice* device, IBuffer* buffer, float* expectedResult, size_t expectedBufferSize)
{
    // Read back the results.
    ComPtr<ISlangBlob> resultBlob;
    REQUIRE_CALL(device->readBuffer(buffer, 0, expectedBufferSize, resultBlob.writeRef()));
    CHECK_EQ(resultBlob->getBufferSize(), expectedBufferSize);
    // Compare results with a tolerance of 0.01.
    auto result = (float*)resultBlob->getBufferPointer();
    compareComputeResultFuzzy(result, expectedResult, expectedBufferSize);
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
    IDevice::Desc deviceDesc = {};
    deviceDesc.deviceType = deviceType;
    deviceDesc.slang.slangGlobalSession = ctx->slangGlobalSession;
    auto searchPaths = getSlangSearchPaths();
    for (const char* path : searchPaths)
        additionalSearchPaths.push_back(path);
    deviceDesc.slang.searchPaths = searchPaths.data();
    deviceDesc.slang.searchPathCount = searchPaths.size();
#if ENABLE_SHADER_CACHE
    deviceDesc.persistentShaderCache = &gShaderCache;
#endif

    D3D12DeviceExtendedDesc extDesc = {};
    extDesc.rootParameterShaderAttributeName = "root";

    SlangSessionExtendedDesc slangExtDesc = {};
    std::vector<slang::CompilerOptionEntry> entries;
    slang::CompilerOptionEntry emitSpirvDirectlyEntry;
    emitSpirvDirectlyEntry.name = slang::CompilerOptionName::EmitSpirvDirectly;
    emitSpirvDirectlyEntry.value.intValue0 = 1;
    entries.push_back(emitSpirvDirectlyEntry);
#if SLANG_RHI_DEBUG_SPIRV
    slang::CompilerOptionEntry debugLevelCompilerOptionEntry;
    debugLevelCompilerOptionEntry.name = slang::CompilerOptionName::DebugInformation;
    debugLevelCompilerOptionEntry.value.intValue0 = SLANG_DEBUG_INFO_LEVEL_STANDARD;
    entries.push_back(debugLevelCompilerOptionEntry);
#endif
    slangExtDesc.compilerOptionEntries = entries.data();
    slangExtDesc.compilerOptionEntryCount = entries.size();

    deviceDesc.extendedDescCount = 2;
    void* extDescPtrs[2] = {&extDesc, &slangExtDesc};
    deviceDesc.extendedDescs = extDescPtrs;

    // TODO: We should also set the debug callback
    // (And in general reduce the differences (and duplication) between
    // here and render-test-main.cpp)
#ifdef _DEBUG
    rhiEnableDebugLayer();
    rhiSetDebugCallback(&sDebugCallback);
#endif

    REQUIRE_CALL(rhiCreateDevice(&deviceDesc, device.writeRef()));

    if (useCachedDevice)
    {
        gCachedDevices[deviceType] = device;
    }

    return device;
}

ComPtr<slang::ISession> createTestingSession(
    GpuTestContext* ctx,
    DeviceType deviceType,
    std::vector<const char*> additionalSearchPaths
)
{
    // Next, load the precompiled slang program.
    ComPtr<slang::ISession> session;
    slang::SessionDesc sessionDesc = {};
    auto searchPaths = getSlangSearchPaths();
    for (const char* path : searchPaths)
        additionalSearchPaths.push_back(path);
    sessionDesc.searchPaths = searchPaths.data();
    sessionDesc.searchPathCount = searchPaths.size();
    sessionDesc.targetCount = 1;
    slang::TargetDesc targetDesc = {};
    switch (deviceType)
    {
    case DeviceType::D3D12:
        targetDesc.format = SLANG_DXIL;
        break;
    case DeviceType::Vulkan:
        targetDesc.format = SLANG_SPIRV;
        break;
    }
    sessionDesc.targets = &targetDesc;
    ctx->slangGlobalSession->createSession(sessionDesc, session.writeRef());
    return session;
}

std::vector<const char*> getSlangSearchPaths()
{
    return {
        "",
        "../../tests",
        "tests",
    };
}

#if SLANG_RHI_ENABLE_RENDERDOC
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
    default:
        return "unknown";
    }
}

void runGpuTests(GpuTestFunc func, std::initializer_list<DeviceType> deviceTypes)
{
    for (auto deviceType : deviceTypes)
    {
        if (!rhiIsDeviceTypeSupported(deviceType))
        {
            continue;
        }
        SUBCASE(deviceTypeToString(deviceType))
        {
            GpuTestContext ctx;
            ctx.slangGlobalSession = getSlangGlobalSession();
            func(&ctx, deviceType);
        }
    }
}

} // namespace rhi::testing
