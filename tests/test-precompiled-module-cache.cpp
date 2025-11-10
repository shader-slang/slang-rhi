#if 0 // TODO_TESTING port

#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

ComPtr<slang::ISession> createSession(gfx::IDevice* device, ISlangFileSystemExt* fileSys)
{
    ComPtr<slang::ISession> slangSession;
    device->getSlangSession(slangSession.writeRef());
    slang::SessionDesc sessionDesc = {};
    sessionDesc.searchPathCount = 1;
    const char* searchPath = "cache/";
    sessionDesc.searchPaths = &searchPath;
    sessionDesc.targetCount = 1;
    sessionDesc.compilerOptionEntryCount = 1;
    slang::CompilerOptionEntry entry;
    entry.name = slang::CompilerOptionName::UseUpToDateBinaryModule;
    entry.value.kind = slang::CompilerOptionValueKind::Int;
    entry.value.intValue0 = 1;
    sessionDesc.compilerOptionEntries = &entry;
    slang::TargetDesc targetDesc = {};
    switch (device->getDeviceType())
    {
    case gfx::DeviceType::D3D12:
        targetDesc.format = SLANG_DXIL;
        targetDesc.profile = device->getSlangSession()->getGlobalSession()->findProfile("sm_6_1");
        break;
    case gfx::DeviceType::Vulkan:
        targetDesc.format = SLANG_SPIRV;
        targetDesc.profile = device->getSlangSession()->getGlobalSession()->findProfile("GLSL_460");
        break;
    }
    sessionDesc.targets = &targetDesc;
    sessionDesc.fileSystem = fileSys;
    auto globalSession = slangSession->getGlobalSession();
    globalSession->createSession(sessionDesc, slangSession.writeRef());
    return slangSession;
}

static Result precompileProgram(
    gfx::IDevice* device,
    ISlangMutableFileSystem* fileSys,
    const char* shaderModuleName
)
{
    ComPtr<slang::ISession> slangSession = createSession(device, fileSys);

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
            fileSys->saveFileBlob(
                (Slang::String("cache/") + Slang::String(name) + ".slang-module").getBuffer(),
                outBlob
            );
        }
    }
    return SLANG_OK;
}

void precompiledModuleCacheTestImpl(IDevice* device, UnitTestContext* context)
{
    // First, Initialize our file system.
    ComPtr<ISlangMutableFileSystem> memoryFileSystem = ComPtr<ISlangMutableFileSystem>(new Slang::MemoryFileSystem());
    memoryFileSystem->createDirectory("cache");

    const char* moduleSrc = R"(
        import "precompiled-module-imported";

        // Main entry-point.

        using namespace ns;

        [shader("compute")]
        [numthreads(4, 1, 1)]
        void computeMain(
            uint3 sv_dispatchThreadID : SV_DispatchThreadID,
            uniform RWStructuredBuffer <float> buffer)
        {
            buffer[sv_dispatchThreadID.x] = helperFunc() + helperFunc1();
        }
    )";
    memoryFileSystem->saveFile("precompiled-module.slang", moduleSrc, strlen(moduleSrc));

    const char* moduleSrc2 = R"(
        module "precompiled-module-imported";

        __include "precompiled-module-included.slang";

        namespace ns
        {
            public int helperFunc()
            {
                return 1;
            }
        }
    )";
    memoryFileSystem->saveFile("precompiled-module-imported.slang", moduleSrc2, strlen(moduleSrc2));
    const char* moduleSrc3 = R"(
        implementing "precompiled-module-imported";

        namespace ns
        {
            public int helperFunc1()
            {
                return 2;
            }
        }
    )";
    memoryFileSystem->saveFile("precompiled-module-included.slang", moduleSrc3, strlen(moduleSrc3));

    // Precompile a module.
    ComPtr<IShaderProgram> shaderProgram;
    REQUIRE_CALL(precompileProgram(device, memoryFileSystem.get(), "precompiled-module-imported"));

    // Next, load the precompiled slang program.
    ComPtr<slang::ISession> slangSession = createSession(device, memoryFileSystem);
    ComPtr<ISlangBlob> binaryBlob;
    memoryFileSystem->loadFile("cache/precompiled-module-imported.slang-module", binaryBlob.writeRef());
    auto upToDate = slangSession->isBinaryModuleUpToDate("precompiled-module-imported.slang", binaryBlob);
    SLANG_CHECK(upToDate); // The module should be up-to-date.

    REQUIRE_CALL(
        loadProgram(device, slangSession, "precompiled-module", "computeMain", shaderProgram)
    );

    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    ComPtr<gfx::IPipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

    const int numberCount = 4;
    float initialData[] = {0.0f, 0.0f, 0.0f, 0.0f};
    BufferDesc bufferDesc = {};
    bufferDesc.size = numberCount * sizeof(float);
    bufferDesc.format = gfx::Format::Undefined;
    bufferDesc.elementSize = sizeof(float);
    bufferDesc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess | BufferUsage::CopyDestination | BufferUsage::CopySource;
    bufferDesc.defaultState = ResourceState::UnorderedAccess;
    bufferDesc.memoryType = MemoryType::DeviceLocal;

    ComPtr<IBuffer> numbersBuffer;
    REQUIRE_CALL(device->createBuffer(bufferDesc, (void*)initialData, numbersBuffer.writeRef()));

    ComPtr<IResourceView> bufferView;
    IResourceView::Desc viewDesc = {};
    viewDesc.type = IResourceView::Type::UnorderedAccess;
    viewDesc.format = Format::Undefined;
    REQUIRE_CALL(device->createBufferView(numbersBuffer, nullptr, viewDesc, bufferView.writeRef()));

    // We have done all the set up work, now it is time to start recording a command buffer for
    // GPU execution.
    {
        ICommandQueue::Desc queueDesc = {ICommandQueue::QueueType::Graphics};
        auto queue = device->createCommandQueue(queueDesc);
        auto commandEncoder = queue->createCommandEncoder();
        auto passEncoder = commandEncoder->beginComputePass();

        auto rootObject = passEncoder->bindPipeline(pipeline);

        ShaderCursor entryPointCursor(rootObject->getEntryPoint(0)); // get a cursor the the first entry-point.
        // Bind buffer view to the entry point.
        entryPointCursor.getPath("buffer").setBinding(bufferView);

        passEncoder->dispatchCompute(1, 1, 1);
        passEncoder->end();
        queue->submit(commandEncoder->finish());
        queue->waitOnHost();
    }

    compareComputeResult(device, numbersBuffer, Slang::makeArray<float>(3.0f, 3.0f, 3.0f, 3.0f));

    // Now we change the source and check if the precompiled module is still up-to-date.
    const char* moduleSrc4 = R"(
        implementing "precompiled-module-imported";
        namespace ns {
            public int helperFunc1() {
                return 2;
            }
        }
    )";
    memoryFileSystem->saveFile("precompiled-module-included.slang", moduleSrc4, strlen(moduleSrc4));

    slangSession = createSession(device, memoryFileSystem);
    upToDate = slangSession->isBinaryModuleUpToDate("precompiled-module-imported.slang", binaryBlob);
    SLANG_CHECK(!upToDate); // The module should not be up-to-date because the source has changed.
}

SLANG_UNIT_TEST(precompiledModuleCacheD3D12)
{
    runTestImpl(precompiledModuleCacheTestImpl, unitTestContext, Slang::RenderApiFlag::D3D12);
}

SLANG_UNIT_TEST(precompiledModuleCacheVulkan)
{
    runTestImpl(precompiledModuleCacheTestImpl, unitTestContext, Slang::RenderApiFlag::Vulkan);
}
#endif
