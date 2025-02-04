#include "testing.h"

#include <filesystem>

using namespace rhi;
using namespace rhi::testing;

static Result precompileProgram(IDevice* device, const char* shaderModuleName, const std::filesystem::path& dir)
{
    ComPtr<slang::ISession> slangSession;
    SLANG_RETURN_ON_FAIL(device->getSlangSession(slangSession.writeRef()));
    slang::SessionDesc sessionDesc = {};
    auto searchPaths = getSlangSearchPaths();
    sessionDesc.searchPaths = searchPaths.data();
    sessionDesc.searchPathCount = searchPaths.size();
    auto globalSession = slangSession->getGlobalSession();
    globalSession->createSession(sessionDesc, slangSession.writeRef());

    ComPtr<slang::IBlob> diagnosticsBlob;
    slang::IModule* module = slangSession->loadModule(shaderModuleName, diagnosticsBlob.writeRef());
    diagnoseIfNeeded(diagnosticsBlob);
    if (!module)
        return SLANG_FAIL;

    // Write loaded modules to memory file system.
    for (SlangInt i = 0; i < slangSession->getLoadedModuleCount(); i++)
    {
        auto module = slangSession->getLoadedModule(i);
        auto path = module->getFilePath();
        if (path)
        {
            auto path = (dir / module->getName()).replace_extension(".slang-module");
            ComPtr<ISlangBlob> outBlob;
            module->serialize(outBlob.writeRef());
            writeFile(path.string(), outBlob->getBufferPointer(), outBlob->getBufferSize());
        }
    }
    return SLANG_OK;
}

void testPrecompiledModule(GpuTestContext* ctx, DeviceType deviceType)
{
    ComPtr<IDevice> device = createTestingDevice(ctx, deviceType);

    std::filesystem::path tempDir = getCaseTempDirectory();
    std::string tempDirStr = tempDir.string();

    ComPtr<IShaderProgram> shaderProgram;
    slang::ProgramLayout* slangReflection;
    REQUIRE_CALL(precompileProgram(device, "test-precompiled-module", tempDir));

    // Next, load the precompiled slang program.
    ComPtr<slang::ISession> slangSession;
    device->getSlangSession(slangSession.writeRef());
    slang::SessionDesc sessionDesc = {};
    sessionDesc.targetCount = 1;
    slang::TargetDesc targetDesc = {};
    switch (device->getDeviceInfo().deviceType)
    {
    case DeviceType::D3D12:
        targetDesc.format = SLANG_DXIL;
        targetDesc.profile = device->getSlangSession()->getGlobalSession()->findProfile("sm_6_1");
        break;
    case DeviceType::Vulkan:
        targetDesc.format = SLANG_SPIRV;
        targetDesc.profile = device->getSlangSession()->getGlobalSession()->findProfile("GLSL_460");
        break;
    }
    sessionDesc.targets = &targetDesc;
    const char* searchPaths[] = {tempDirStr.c_str()};
    sessionDesc.searchPaths = searchPaths;
    sessionDesc.searchPathCount = SLANG_COUNT_OF(searchPaths);
    auto globalSession = slangSession->getGlobalSession();
    globalSession->createSession(sessionDesc, slangSession.writeRef());
    REQUIRE_CALL(loadComputeProgram(
        device,
        slangSession,
        shaderProgram,
        "test-precompiled-module",
        "computeMain",
        slangReflection
    ));

    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

    const int numberCount = 4;
    float initialData[] = {0.0f, 0.0f, 0.0f, 0.0f};
    BufferDesc bufferDesc = {};
    bufferDesc.size = numberCount * sizeof(float);
    bufferDesc.format = Format::Unknown;
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

TEST_CASE("precompiled-module")
{
    runGpuTests(
        testPrecompiledModule,
        {
            // DeviceType::D3D11,
            DeviceType::D3D12,
            DeviceType::Vulkan,
            DeviceType::Metal,
            // DeviceType::CPU,
            // DeviceType::CUDA,
            DeviceType::WGPU,

        }
    );
}
