#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

static Result loadTestProgram(IDevice* device, IShaderProgram** outShaderProgram)
{
    const char* moduleInterfaceSrc = R"(
        interface IBase : IDifferentiable
        {
            [Differentiable]
            __init(int x);
            [Differentiable]
            float getBaseValue();
            [Differentiable]
            static float getBaseValueS();
        }
        interface IFoo : IBase
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
            [Differentiable]
            float getBaseValue() { return val; }
            [Differentiable]
            static float getBaseValueS() { return 0.0; }
            property float val2 {
                get { return val + 2.0; }
                set { val = newValue; }
            }
            [Differentiable]
            __init(int x) { val = x; }
        };
    )";
    const char* module0Src = R"(
        import ifoo;
        extern struct Foo : IFoo;

        [numthreads(1,1,1)]
        void computeMain(uniform RWStructuredBuffer<float> buffer)
        {
            Foo foo = Foo(0);
            foo.setValue(3.0);
            buffer[0] = foo.getValue() + foo.val2 + Foo.offset + foo.getBaseValue();
        }
    )";
    const char* module1Src = R"(
        import ifoo;
        export struct Foo : IFoo = FooImpl;)";
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

    ShaderProgramDesc shaderProgramDesc = {};
    shaderProgramDesc.slangGlobalScope = linkedProgram;
    result = device->createShaderProgram(shaderProgramDesc, outShaderProgram, diagnosticsBlob.writeRef());
    diagnoseIfNeeded(diagnosticsBlob);
    return result;
}

// TODO(testing): CUDA crashes
GPU_TEST_CASE("link-time-type", D3D11 | D3D12 | Vulkan | Metal | CPU | WGPU | DontCacheDevice)
{
    ComPtr<IShaderProgram> shaderProgram;
    REQUIRE_CALL(loadTestProgram(device, shaderProgram.writeRef()));

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
        entryPointCursor["buffer"].setBinding(buffer);
        passEncoder->dispatchCompute(1, 1, 1);
        passEncoder->end();

        queue->submit(commandEncoder->finish());
        queue->waitOnHost();
    }

    compareComputeResult(device, buffer, makeArray<float>(11.f));
}
