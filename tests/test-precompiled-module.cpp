#include "testing.h"

#include <filesystem>

using namespace rhi;
using namespace rhi::testing;

static slang::TargetDesc getTargetDesc(DeviceType deviceType, slang::IGlobalSession* globalSession)
{
    slang::TargetDesc targetDesc = {};
    switch (deviceType)
    {
    case DeviceType::D3D11:
        targetDesc.format = SLANG_DXBC;
        targetDesc.profile = globalSession->findProfile("sm_5_0");
        break;
    case DeviceType::D3D12:
        targetDesc.format = SLANG_DXIL;
        targetDesc.profile = globalSession->findProfile("sm_6_1");
        break;
    case DeviceType::Vulkan:
        targetDesc.format = SLANG_SPIRV;
        targetDesc.profile = globalSession->findProfile("GLSL_460");
        break;
    case DeviceType::Metal:
        targetDesc.format = SLANG_METAL_LIB;
        targetDesc.profile = globalSession->findProfile("");
        break;
    case DeviceType::CPU:
        targetDesc.format = SLANG_SHADER_HOST_CALLABLE;
        targetDesc.profile = globalSession->findProfile("sm_5_0");
        break;
    case DeviceType::CUDA:
        targetDesc.format = SLANG_PTX;
        targetDesc.profile = globalSession->findProfile("sm_5_0");
        break;
    case DeviceType::WGPU:
        targetDesc.format = SLANG_WGSL;
        targetDesc.profile = globalSession->findProfile("");
        break;
    default:
        FAIL("Unsupported device type");
    }
    return targetDesc;
}

static Result precompileProgram(
    IDevice* device,
    const char* shaderModuleName,
    const std::filesystem::path& dir,
    bool precompileToTarget
)
{
    ComPtr<slang::ISession> slangSession;
    SLANG_RETURN_ON_FAIL(device->getSlangSession(slangSession.writeRef()));
    slang::SessionDesc sessionDesc = {};
    auto searchPaths = getSlangSearchPaths();
    sessionDesc.searchPaths = searchPaths.data();
    sessionDesc.searchPathCount = searchPaths.size();
    slang::TargetDesc targetDesc =
        getTargetDesc(device->getDeviceType(), device->getSlangSession()->getGlobalSession());
    sessionDesc.targets = &targetDesc;
    sessionDesc.targetCount = 1;
    auto globalSession = slangSession->getGlobalSession();
    globalSession->createSession(sessionDesc, slangSession.writeRef());

    ComPtr<slang::IBlob> diagnosticsBlob;
    slang::IModule* module = slangSession->loadModule(shaderModuleName, diagnosticsBlob.writeRef());
    diagnoseIfNeeded(diagnosticsBlob);
    if (!module)
        return SLANG_FAIL;

    if (precompileToTarget)
    {
        slang::IModulePrecompileService_Experimental* precompileService = nullptr;
        SLANG_RETURN_ON_FAIL(module->queryInterface(
            slang::IModulePrecompileService_Experimental::getTypeGuid(),
            (void**)&precompileService
        ));

        SlangCompileTarget target;
        switch (device->getDeviceType())
        {
        case DeviceType::D3D12:
            target = SLANG_DXIL;
            break;
        case DeviceType::Vulkan:
            target = SLANG_SPIRV;
            break;
        default:
            return SLANG_FAIL;
        }
        precompileService->precompileForTarget(target, diagnosticsBlob.writeRef());
    }

    // Write loaded modules to files.
    for (SlangInt i = 0; i < slangSession->getLoadedModuleCount(); i++)
    {
        auto loadedModule = slangSession->getLoadedModule(i);
        if (loadedModule->getFilePath())
        {
            auto path = (dir / loadedModule->getName()).replace_extension(".slang-module");
            ComPtr<ISlangBlob> outBlob;
            loadedModule->serialize(outBlob.writeRef());
            writeFile(path.string(), outBlob->getBufferPointer(), outBlob->getBufferSize());
        }
    }
    return SLANG_OK;
}

// mixed == false : precompile `test-precompiled-module` and then load it.
// mixed == true : only precompile `test-precompiled-module-imported` and the load `test-precompiled-module`.
static void testPrecompiledModuleImpl(IDevice* device, bool mixed, bool precompileToTarget)
{
    std::filesystem::path tempDir = getCaseTempDirectory();
    std::string tempDirStr = tempDir.string();

    ComPtr<IShaderProgram> shaderProgram;
    REQUIRE_CALL(precompileProgram(
        device,
        mixed ? "test-precompiled-module-imported" : "test-precompiled-module",
        tempDir,
        precompileToTarget
    ));

    if (mixed)
        std::filesystem::copy_file(
            std::filesystem::path(getTestsDir()) / "test-precompiled-module.slang",
            tempDir / "test-precompiled-module.slang",
            std::filesystem::copy_options::overwrite_existing
        );

    // Next, load the slang program.
    ComPtr<slang::ISession> slangSession;
    device->getSlangSession(slangSession.writeRef());
    slang::SessionDesc sessionDesc = {};
    slang::TargetDesc targetDesc =
        getTargetDesc(device->getDeviceType(), device->getSlangSession()->getGlobalSession());
    sessionDesc.targets = &targetDesc;
    sessionDesc.targetCount = 1;
    const char* searchPaths[] = {tempDirStr.c_str()};
    sessionDesc.searchPaths = searchPaths;
    sessionDesc.searchPathCount = SLANG_COUNT_OF(searchPaths);
    auto globalSession = slangSession->getGlobalSession();
    globalSession->createSession(sessionDesc, slangSession.writeRef());
    REQUIRE_CALL(
        loadAndLinkProgram(device, slangSession, "test-precompiled-module", "computeMain", shaderProgram.writeRef())
    );

    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

    const int numberCount = 4;
    float initialData[] = {0.0f, 0.0f, 0.0f, 0.0f};
    BufferDesc bufferDesc = {};
    bufferDesc.size = numberCount * sizeof(float);
    bufferDesc.format = Format::Undefined;
    bufferDesc.elementSize = sizeof(float);
    bufferDesc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess | BufferUsage::CopyDestination |
                       BufferUsage::CopySource;
    bufferDesc.defaultState = ResourceState::UnorderedAccess;
    bufferDesc.memoryType = MemoryType::DeviceLocal;

    ComPtr<IBuffer> buffer;
    REQUIRE_CALL(device->createBuffer(bufferDesc, (void*)initialData, buffer.writeRef()));

    // We have done all the set up work, now it is time to start recording a command buffer for
    // GPU execution.
    {
        auto queue = device->getQueue(QueueType::Graphics);
        auto commandEncoder = queue->createCommandEncoder();
        auto passEncoder = commandEncoder->beginComputePass();
        auto rootObject = passEncoder->bindPipeline(pipeline);
        ShaderCursor entryPointCursor(rootObject->getEntryPoint(0)); // get a cursor the the first entry-point.
        // Bind buffer view to the entry point.
        entryPointCursor["buffer"].setBinding(buffer);
        passEncoder->dispatchCompute(1, 1, 1);
        passEncoder->end();

        queue->submit(commandEncoder->finish());
        queue->waitOnHost();
    }

    compareComputeResult(device, buffer, makeArray<float>(3.0f, 3.0f, 3.0f, 3.0f));
}

// CUDA: currently fails due to a slang regression
// https://github.com/shader-slang/slang/issues/7315
GPU_TEST_CASE("precompiled-module", ALL)
{
    testPrecompiledModuleImpl(device, false, false);
}

// CUDA: currently fails due to a slang regression
// https://github.com/shader-slang/slang/issues/7315
GPU_TEST_CASE("precompiled-module-mixed", ALL)
{
    testPrecompiledModuleImpl(device, true, false);
}

// TODO this currently fails
// GPU_TEST_CASE("precompiled-module-with-target-code", D3D12)
// {
//     testPrecompiledModuleImpl(device, false, true);
// }
