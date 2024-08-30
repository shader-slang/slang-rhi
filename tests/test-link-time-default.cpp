#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

static Result loadProgram(
    IDevice* device,
    ComPtr<IShaderProgram>& outShaderProgram,
    slang::ProgramLayout*& slangReflection,
    bool linkSpecialization = false
)
{
    const char* moduleInterfaceSrc = R"(
        interface IFoo
        {
            static const int offset;
            [mutating] void setValue(float v);
            float getValue();
            property float val2{get;set;}
        }
        struct FooImpl : IFoo
        {
            float val;
            static const int offset = -1;
            [mutating] void setValue(float v) { val = v; }
            float getValue() { return val + 1.0; }
            property float val2 {
                get { return val + 2.0; }
                set { val = newValue; }
            }
        };
        struct BarImpl : IFoo
        {
            float val;
            static const int offset = 2;
            [mutating] void setValue(float v) { val = v; }
            float getValue() { return val + 1.0; }
            property float val2 {
                get { return val; }
                set { val = newValue; }
            }
        };
    )";
    const char* module0Src = R"(
        import ifoo;
        extern struct Foo : IFoo = FooImpl;
        extern static const float c = 0.0;
        [numthreads(1,1,1)]
        void computeMain(uniform RWStructuredBuffer<float> buffer)
        {
            Foo foo;
            foo.setValue(3.0);
            buffer[0] = foo.getValue() + foo.val2 + Foo.offset + c;
        }
    )";
    const char* module1Src = R"(
        import ifoo;
        export struct Foo : IFoo = BarImpl;
        export static const float c = 1.0;
    )";
    ComPtr<slang::ISession> slangSession;
    SLANG_RETURN_ON_FAIL(device->getSlangSession(slangSession.writeRef()));
    ComPtr<slang::IBlob> diagnosticsBlob;
    auto moduleInterfaceBlob = UnownedBlob::create(moduleInterfaceSrc, strlen(moduleInterfaceSrc));
    auto module0Blob = UnownedBlob::create(module0Src, strlen(module0Src));
    auto module1Blob = UnownedBlob::create(module1Src, strlen(module1Src));
    slang::IModule* moduleInterface = slangSession->loadModuleFromSource("ifoo", "ifoo.slang", moduleInterfaceBlob);
    slang::IModule* module0 = slangSession->loadModuleFromSource("module0", "path0", module0Blob);
    slang::IModule* module1 = slangSession->loadModuleFromSource("module1", "path1", module1Blob);
    ComPtr<slang::IEntryPoint> computeEntryPoint;
    SLANG_RETURN_ON_FAIL(module0->findEntryPointByName("computeMain", computeEntryPoint.writeRef()));

    std::vector<slang::IComponentType*> componentTypes;
    componentTypes.push_back(moduleInterface);
    componentTypes.push_back(module0);
    if (linkSpecialization)
        componentTypes.push_back(module1);
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

    composedProgram = linkedProgram;
    slangReflection = composedProgram->getLayout();

    IShaderProgram::Desc programDesc = {};
    programDesc.slangGlobalScope = composedProgram.get();

    auto shaderProgram = device->createProgram(programDesc);

    outShaderProgram = shaderProgram;
    return SLANG_OK;
}

void testLinkTimeDefault(GpuTestContext* ctx, DeviceType deviceType)
{
    ComPtr<IDevice> device = createTestingDevice(ctx, deviceType);

    ComPtr<ITransientResourceHeap> transientHeap;
    ITransientResourceHeap::Desc transientHeapDesc = {};
    transientHeapDesc.constantBufferSize = 4096;
    REQUIRE_CALL(device->createTransientResourceHeap(transientHeapDesc, transientHeap.writeRef()));

    // Create pipeline without linking a specialization override module, so we should
    // see the default value of `extern Foo`.
    ComPtr<IShaderProgram> shaderProgram;
    slang::ProgramLayout* slangReflection;
    REQUIRE_CALL(loadProgram(device, shaderProgram, slangReflection, false));

    ComputePipelineStateDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    ComPtr<IPipelineState> pipelineState;
    REQUIRE_CALL(device->createComputePipelineState(pipelineDesc, pipelineState.writeRef()));

    // Create pipeline with a specialization override module linked in, so we should
    // see the result of using `Bar` for `extern Foo`.
    ComPtr<IShaderProgram> shaderProgram1;
    REQUIRE_CALL(loadProgram(device, shaderProgram1, slangReflection, true));

    ComputePipelineStateDesc pipelineDesc1 = {};
    pipelineDesc1.program = shaderProgram1.get();
    ComPtr<IPipelineState> pipelineState1;
    REQUIRE_CALL(device->createComputePipelineState(pipelineDesc1, pipelineState1.writeRef()));

    const int numberCount = 4;
    float initialData[] = {0.0f, 0.0f, 0.0f, 0.0f};
    IBufferResource::Desc bufferDesc = {};
    bufferDesc.sizeInBytes = numberCount * sizeof(float);
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

    ComPtr<IBufferResource> numbersBuffer;
    REQUIRE_CALL(device->createBufferResource(bufferDesc, (void*)initialData, numbersBuffer.writeRef()));

    ComPtr<IResourceView> bufferView;
    IResourceView::Desc viewDesc = {};
    viewDesc.type = IResourceView::Type::UnorderedAccess;
    viewDesc.format = Format::Unknown;
    REQUIRE_CALL(device->createBufferView(numbersBuffer, nullptr, viewDesc, bufferView.writeRef()));

    ICommandQueue::Desc queueDesc = {ICommandQueue::QueueType::Graphics};
    auto queue = device->createCommandQueue(queueDesc);

    // We have done all the set up work, now it is time to start recording a command buffer for
    // GPU execution.
    {
        auto commandBuffer = transientHeap->createCommandBuffer();
        auto encoder = commandBuffer->encodeComputeCommands();

        auto rootObject = encoder->bindPipeline(pipelineState);

        ShaderCursor entryPointCursor(rootObject->getEntryPoint(0)); // get a cursor the the first entry-point.
        // Bind buffer view to the entry point.
        entryPointCursor.getPath("buffer").setResource(bufferView);

        encoder->dispatchCompute(1, 1, 1);
        encoder->endEncoding();
        commandBuffer->close();
        queue->executeCommandBuffer(commandBuffer);
        queue->waitOnHost();
    }

    compareComputeResult(device, numbersBuffer, makeArray<float>(8.f));

    // Now run again with the overrided program.
    {
        auto commandBuffer = transientHeap->createCommandBuffer();
        auto encoder = commandBuffer->encodeComputeCommands();

        auto rootObject = encoder->bindPipeline(pipelineState1);

        ShaderCursor entryPointCursor(rootObject->getEntryPoint(0)); // get a cursor the the first entry-point.
        // Bind buffer view to the entry point.
        entryPointCursor.getPath("buffer").setResource(bufferView);

        encoder->dispatchCompute(1, 1, 1);
        encoder->endEncoding();
        commandBuffer->close();
        queue->executeCommandBuffer(commandBuffer);
        queue->waitOnHost();
    }

    compareComputeResult(device, numbersBuffer, makeArray<float>(10.f));
}

TEST_CASE("link-time-default")
{
    runGpuTests(testLinkTimeDefault, {DeviceType::D3D12, DeviceType::Vulkan});
}
