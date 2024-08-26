// SPDX-License-Identifier: Apache-2.0

#include "testing.h"

#include <algorithm>
#include <map>
#include <ctime>
#include <cctype>

namespace gfx::testing {

void diagnoseIfNeeded(slang::IBlob* diagnosticsBlob)
{
    if (diagnosticsBlob != nullptr)
    {
        MESSAGE((const char*)diagnosticsBlob->getBufferPointer());
    }
}

Slang::Result loadComputeProgram(
    gfx::IDevice* device,
    Slang::ComPtr<gfx::IShaderProgram>& outShaderProgram,
    const char* shaderModuleName,
    const char* entryPointName,
    slang::ProgramLayout*& slangReflection)
{
    Slang::ComPtr<slang::ISession> slangSession;
    SLANG_RETURN_ON_FAIL(device->getSlangSession(slangSession.writeRef()));
    Slang::ComPtr<slang::IBlob> diagnosticsBlob;
    slang::IModule* module = slangSession->loadModule(shaderModuleName, diagnosticsBlob.writeRef());
    diagnoseIfNeeded(diagnosticsBlob);
    if (!module)
        return SLANG_FAIL;

    ComPtr<slang::IEntryPoint> computeEntryPoint;
    SLANG_RETURN_ON_FAIL(
        module->findEntryPointByName(entryPointName, computeEntryPoint.writeRef()));

    std::vector<slang::IComponentType*> componentTypes;
    componentTypes.push_back(module);
    componentTypes.push_back(computeEntryPoint);

    Slang::ComPtr<slang::IComponentType> composedProgram;
    SlangResult result = slangSession->createCompositeComponentType(
        componentTypes.data(),
        componentTypes.size(),
        composedProgram.writeRef(),
        diagnosticsBlob.writeRef());
    diagnoseIfNeeded(diagnosticsBlob);
    SLANG_RETURN_ON_FAIL(result);

    ComPtr<slang::IComponentType> linkedProgram;
    result = composedProgram->link(linkedProgram.writeRef(), diagnosticsBlob.writeRef());
    diagnoseIfNeeded(diagnosticsBlob);
    SLANG_RETURN_ON_FAIL(result);

    composedProgram = linkedProgram;
    slangReflection = composedProgram->getLayout();

    gfx::IShaderProgram::Desc programDesc = {};
    programDesc.slangGlobalScope = composedProgram.get();

    auto shaderProgram = device->createProgram(programDesc);

    outShaderProgram = shaderProgram;
    return SLANG_OK;
}

Slang::Result loadComputeProgram(
    gfx::IDevice* device,
    slang::ISession* slangSession,
    Slang::ComPtr<gfx::IShaderProgram>& outShaderProgram,
    const char* shaderModuleName,
    const char* entryPointName,
    slang::ProgramLayout*& slangReflection)
{
    Slang::ComPtr<slang::IBlob> diagnosticsBlob;
    slang::IModule* module = slangSession->loadModule(shaderModuleName, diagnosticsBlob.writeRef());
    diagnoseIfNeeded(diagnosticsBlob);
    if (!module)
        return SLANG_FAIL;

    ComPtr<slang::IEntryPoint> computeEntryPoint;
    SLANG_RETURN_ON_FAIL(
        module->findEntryPointByName(entryPointName, computeEntryPoint.writeRef()));

    std::vector<slang::IComponentType*> componentTypes;
    componentTypes.push_back(module);
    componentTypes.push_back(computeEntryPoint);

    Slang::ComPtr<slang::IComponentType> composedProgram;
    SlangResult result = slangSession->createCompositeComponentType(
        componentTypes.data(),
        componentTypes.size(),
        composedProgram.writeRef(),
        diagnosticsBlob.writeRef());
    diagnoseIfNeeded(diagnosticsBlob);
    SLANG_RETURN_ON_FAIL(result);

    ComPtr<slang::IComponentType> linkedProgram;
    result = composedProgram->link(linkedProgram.writeRef(), diagnosticsBlob.writeRef());
    diagnoseIfNeeded(diagnosticsBlob);
    SLANG_RETURN_ON_FAIL(result);

    composedProgram = linkedProgram;
    slangReflection = composedProgram->getLayout();

    gfx::IShaderProgram::Desc programDesc = {};
    programDesc.slangGlobalScope = composedProgram.get();

    auto shaderProgram = device->createProgram(programDesc);

    outShaderProgram = shaderProgram;
    return SLANG_OK;
}

Slang::Result loadComputeProgramFromSource(
    gfx::IDevice* device,
    Slang::ComPtr<gfx::IShaderProgram>& outShaderProgram,
    std::string_view source)
{
    Slang::ComPtr<slang::IBlob> diagnosticsBlob;

    gfx::IShaderProgram::CreateDesc2 programDesc = {};
    programDesc.sourceType = gfx::ShaderModuleSourceType::SlangSource;
    programDesc.sourceData = (void*)source.data();
    programDesc.sourceDataSize = source.size();

    return device->createProgram2(programDesc, outShaderProgram.writeRef(), diagnosticsBlob.writeRef());
}

Slang::Result loadGraphicsProgram(
    gfx::IDevice* device,
    Slang::ComPtr<gfx::IShaderProgram>& outShaderProgram,
    const char* shaderModuleName,
    const char* vertexEntryPointName,
    const char* fragmentEntryPointName,
    slang::ProgramLayout*& slangReflection)
{
    Slang::ComPtr<slang::ISession> slangSession;
    SLANG_RETURN_ON_FAIL(device->getSlangSession(slangSession.writeRef()));
    Slang::ComPtr<slang::IBlob> diagnosticsBlob;
    slang::IModule* module = slangSession->loadModule(shaderModuleName, diagnosticsBlob.writeRef());
    diagnoseIfNeeded(diagnosticsBlob);
    if (!module)
        return SLANG_FAIL;

    ComPtr<slang::IEntryPoint> vertexEntryPoint;
    SLANG_RETURN_ON_FAIL(
        module->findEntryPointByName(vertexEntryPointName, vertexEntryPoint.writeRef()));

    ComPtr<slang::IEntryPoint> fragmentEntryPoint;
    SLANG_RETURN_ON_FAIL(
        module->findEntryPointByName(fragmentEntryPointName, fragmentEntryPoint.writeRef()));

    std::vector<slang::IComponentType*> componentTypes;
    componentTypes.push_back(module);
    componentTypes.push_back(vertexEntryPoint);
    componentTypes.push_back(fragmentEntryPoint);

    Slang::ComPtr<slang::IComponentType> composedProgram;
    SlangResult result = slangSession->createCompositeComponentType(
        componentTypes.data(),
        componentTypes.size(),
        composedProgram.writeRef(),
        diagnosticsBlob.writeRef());
    diagnoseIfNeeded(diagnosticsBlob);
    SLANG_RETURN_ON_FAIL(result);
    slangReflection = composedProgram->getLayout();

    gfx::IShaderProgram::Desc programDesc = {};
    programDesc.slangGlobalScope = composedProgram.get();

    auto shaderProgram = device->createProgram(programDesc);

    outShaderProgram = shaderProgram;
    return SLANG_OK;
}

void compareComputeResult(
    gfx::IDevice* device,
    gfx::ITextureResource* texture,
    gfx::ResourceState state,
    void* expectedResult,
    size_t expectedResultRowPitch,
    size_t rowCount)
{
    // Read back the results.
    ComPtr<ISlangBlob> resultBlob;
    size_t rowPitch = 0;
    size_t pixelSize = 0;
    GFX_CHECK_CALL_ABORT(device->readTextureResource(
        texture, state, resultBlob.writeRef(), &rowPitch, &pixelSize));
    // Compare results.
    for (size_t row = 0; row < rowCount; row++)
    {
        CHECK(
            memcmp(
                (uint8_t*)resultBlob->getBufferPointer() + rowPitch * row,
                (uint8_t*)expectedResult + expectedResultRowPitch * row,
                expectedResultRowPitch) == 0);
    }
}

void compareComputeResult(gfx::IDevice* device, gfx::IBufferResource* buffer, size_t offset, const void* expectedResult, size_t expectedBufferSize)
{
    // Read back the results.
    ComPtr<ISlangBlob> resultBlob;
    GFX_CHECK_CALL_ABORT(device->readBufferResource(
        buffer, offset, expectedBufferSize, resultBlob.writeRef()));
    CHECK_EQ(resultBlob->getBufferSize(), expectedBufferSize);
    // Compare results.
    CHECK(memcmp(resultBlob->getBufferPointer(), (uint8_t*)expectedResult, expectedBufferSize) == 0);
}

void compareComputeResultFuzzy(const float* result, float* expectedResult, size_t expectedBufferSize)
{
    for (size_t i = 0; i < expectedBufferSize / sizeof(float); ++i)
    {
        CHECK_LE(abs(result[i] - expectedResult[i]), 0.01);
    }
}

void compareComputeResultFuzzy(gfx::IDevice* device, gfx::IBufferResource* buffer, float* expectedResult, size_t expectedBufferSize)
{
    // Read back the results.
    ComPtr<ISlangBlob> resultBlob;
    GFX_CHECK_CALL_ABORT(device->readBufferResource(
        buffer, 0, expectedBufferSize, resultBlob.writeRef()));
    CHECK_EQ(resultBlob->getBufferSize(), expectedBufferSize);
    // Compare results with a tolerance of 0.01.
    auto result = (float*)resultBlob->getBufferPointer();
    compareComputeResultFuzzy(result, expectedResult, expectedBufferSize);
}

std::map<DeviceType, Slang::ComPtr<IDevice>> cachedDevices;

Slang::ComPtr<gfx::IDevice> createTestingDevice(
    GpuTestContext* ctx,
    DeviceType deviceType,
    bool useCachedDevice,
    std::vector<const char*> additionalSearchPaths)
{
    if (useCachedDevice)
    {
        auto it = cachedDevices.find(deviceType);
        if (it != cachedDevices.end())
        {
            return it->second;
        }
    }

    Slang::ComPtr<gfx::IDevice> device;
    gfx::IDevice::Desc deviceDesc = {};
    deviceDesc.deviceType = deviceType;
    deviceDesc.slang.slangGlobalSession = ctx->slangGlobalSession;
    auto searchPaths = getSlangSearchPaths();
    for (const char* path : searchPaths)
        additionalSearchPaths.push_back(path);
    deviceDesc.slang.searchPaths = searchPaths.data();
    deviceDesc.slang.searchPathCount = searchPaths.size();

    gfx::D3D12DeviceExtendedDesc extDesc = {};
    extDesc.rootParameterShaderAttributeName = "root";
    
    gfx::SlangSessionExtendedDesc slangExtDesc = {};
    std::vector<slang::CompilerOptionEntry> entries;
    slang::CompilerOptionEntry emitSpirvDirectlyEntry;
    emitSpirvDirectlyEntry.name = slang::CompilerOptionName::EmitSpirvDirectly;
    emitSpirvDirectlyEntry.value.intValue0 = 1;
    entries.push_back(emitSpirvDirectlyEntry);
#if GFX_ENABLE_SPIRV_DEBUG
    slang::CompilerOptionEntry debugLevelCompilerOptionEntry;
    debugLevelCompilerOptionEntry.name = slang::CompilerOptionName::DebugInformation;
    debugLevelCompilerOptionEntry.value.intValue0 = SLANG_DEBUG_INFO_LEVEL_STANDARD;
    entries.push_back(debugLevelCompilerOptionEntry);
#endif
    slangExtDesc.compilerOptionEntries = entries.data();
    slangExtDesc.compilerOptionEntryCount = entries.size();

    deviceDesc.extendedDescCount = 2;
    void* extDescPtrs[2] = { &extDesc, &slangExtDesc };
    deviceDesc.extendedDescs = extDescPtrs;

    // TODO: We should also set the debug callback
    // (And in general reduce the differences (and duplication) between
    // here and render-test-main.cpp)
#ifdef _DEBUG
    gfx::gfxEnableDebugLayer();
#endif

    GFX_CHECK_CALL_ABORT(gfxCreateDevice(&deviceDesc, device.writeRef()));

    if (useCachedDevice)
    {
        cachedDevices[deviceType] = device;
    }

    return device;
}

std::vector<const char*> getSlangSearchPaths()
{
    return {
        "",
        "../../tests",
        "tests",
    };
}

#if GFX_ENABLE_RENDERDOC_INTEGRATION
RENDERDOC_API_1_1_2* rdoc_api = NULL;
void initializeRenderDoc()
{
    if (HMODULE mod = GetModuleHandleA("renderdoc.dll"))
    {
        pRENDERDOC_GetAPI RENDERDOC_GetAPI =
            (pRENDERDOC_GetAPI)GetProcAddress(mod, "RENDERDOC_GetAPI");
        int ret = RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_1_2, (void**)&rdoc_api);
        assert(ret == 1);
    }
}
void renderDocBeginFrame()
{
    if (!rdoc_api) initializeRenderDoc();
    if (rdoc_api) rdoc_api->StartFrameCapture(nullptr, nullptr);
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
    std::transform(adapterName.begin(), adapterName.end(), adapterName.begin(), [](unsigned char c){ return std::tolower(c); });
    return adapterName.find("swiftshader") != std::string::npos;
}

static slang::IGlobalSession* getSlangGlobalSession()
{
    static slang::IGlobalSession* slangGlobalSession = []()
    {
        slang::IGlobalSession* session;
        GFX_CHECK_CALL_ABORT(slang::createGlobalSession(&session));
        return session;
    }();
    return slangGlobalSession;
}

inline const char* deviceTypeToString(DeviceType deviceType)
{
    switch (deviceType)
    {
    case DeviceType::D3D12:
        return "D3D12";
    case DeviceType::Vulkan:
        return "Vulkan";
    case DeviceType::Metal:
        return "Metal";
    case DeviceType::CPU:
        return "CPU";
    case DeviceType::CUDA:
        return "CUDA";
    default:
        return "Unknown";
    }
}

void runGpuTests(GpuTestFunc func, std::initializer_list<DeviceType> deviceTypes)
{
    for (auto deviceType : deviceTypes)
    {
        SUBCASE(deviceTypeToString(deviceType))
        {
            GpuTestContext ctx;
            ctx.slangGlobalSession = getSlangGlobalSession();
            func(&ctx, deviceType);
        }
    }
}

} // namespace gfx::testing
