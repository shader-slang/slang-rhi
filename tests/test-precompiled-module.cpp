#if 0 // TODO_TESTING use filesystem
#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

static Result precompileProgram(IDevice* device, ISlangMutableFileSystem* fileSys, const char* shaderModuleName)
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
            auto name = string::from_cstr(module->getName());
            ComPtr<ISlangBlob> outBlob;
            module->serialize(outBlob.writeRef());
            fileSys->saveFileBlob((name + ".slang-module").c_str(), outBlob);
        }
    }
    return SLANG_OK;
}

void testPrecompiledModule(GpuTestContext* ctx, DeviceType deviceType)
{
    ComPtr<IDevice> device = createTestingDevice(ctx, deviceType);

    ComPtr<ITransientResourceHeap> transientHeap;
    ITransientResourceHeap::Desc transientHeapDesc = {};
    transientHeapDesc.constantBufferSize = 4096;
    REQUIRE_CALL(device->createTransientResourceHeap(transientHeapDesc, transientHeap.writeRef()));

    // First, load and compile the slang source.
    ComPtr<ISlangMutableFileSystem> memoryFileSystem = ComPtr<ISlangMutableFileSystem>(new Slang::MemoryFileSystem());

    ComPtr<IShaderProgram> shaderProgram;
    slang::ProgramLayout* slangReflection;
    REQUIRE_CALL(precompileProgram(device, memoryFileSystem.get(), "precompiled-module"));

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
    sessionDesc.fileSystem = memoryFileSystem.get();
    auto globalSession = slangSession->getGlobalSession();
    globalSession->createSession(sessionDesc, slangSession.writeRef());
    REQUIRE_CALL(
        loadComputeProgram(device, slangSession, shaderProgram, "precompiled-module", "computeMain", slangReflection)
    );

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
    bufferDesc.allowedStates = ResourceStateSet(
        ResourceState::ShaderResource,
        ResourceState::UnorderedAccess,
        ResourceState::CopyDestination,
        ResourceState::CopySource
    );
    bufferDesc.defaultState = ResourceState::UnorderedAccess;
    bufferDesc.memoryType = MemoryType::DeviceLocal;

    ComPtr<IBuffer> numbersBuffer;
    REQUIRE_CALL(device->createBuffer(bufferDesc, (void*)initialData, numbersBuffer.writeRef()));

    ComPtr<IResourceView> bufferView;
    IResourceView::Desc viewDesc = {};
    viewDesc.type = IResourceView::Type::UnorderedAccess;
    viewDesc.format = Format::Unknown;
    REQUIRE_CALL(device->createBufferView(numbersBuffer, nullptr, viewDesc, bufferView.writeRef()));

    // We have done all the set up work, now it is time to start recording a command buffer for
    // GPU execution.
    {
        ICommandQueue::Desc queueDesc = {ICommandQueue::QueueType::Graphics};
        auto queue = device->createCommandQueue(queueDesc);

        auto commandBuffer = transientHeap->createCommandBuffer();
        auto encoder = commandBuffer->encodeComputeCommands();

        auto rootObject = encoder->bindPipeline(pipeline);

        ShaderCursor entryPointCursor(rootObject->getEntryPoint(0)); // get a cursor the the first entry-point.
        // Bind buffer view to the entry point.
        entryPointCursor.getPath("buffer").setResource(bufferView);

        encoder->dispatchCompute(1, 1, 1);
        encoder->endEncoding();
        commandBuffer->close();
        queue->executeCommandBuffer(commandBuffer);
        queue->waitOnHost();
    }

    compareComputeResult(device, numbersBuffer, makeArray<float>(3.0f, 3.0f, 3.0f, 3.0f));
}

TEST_CASE("precompiled-module")
{
    runGpuTests(testPrecompiledModule, {DeviceType::D3D12, DeviceType::Vulkan});
}
#endif
