#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

static Result loadTestProgram(
    IDevice* device,
    const char* shaderModuleName,
    const char* entryPointName,
    const char* additionalModuleSource,
    IShaderProgram** outShaderProgram,
    slang::ProgramLayout** outSlangReflection = nullptr
)
{
    ComPtr<slang::ISession> slangSession;
    SLANG_RETURN_ON_FAIL(device->getSlangSession(slangSession.writeRef()));
    ComPtr<slang::IBlob> diagnosticsBlob;
    slang::IModule* module = slangSession->loadModule(shaderModuleName, diagnosticsBlob.writeRef());
    diagnoseIfNeeded(diagnosticsBlob);
    if (!module)
        return SLANG_FAIL;

    auto additionalModuleBlob = UnownedBlob::create(additionalModuleSource, strlen(additionalModuleSource));
    slang::IModule* additionalModule =
        slangSession->loadModuleFromSource("linkedConstants", "path", additionalModuleBlob);

    ComPtr<slang::IEntryPoint> computeEntryPoint;
    SLANG_RETURN_ON_FAIL(module->findEntryPointByName(entryPointName, computeEntryPoint.writeRef()));

    std::vector<slang::IComponentType*> componentTypes;
    componentTypes.push_back(module);
    componentTypes.push_back(computeEntryPoint);
    componentTypes.push_back(additionalModule);

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
    if (outSlangReflection)
    {
        *outSlangReflection = linkedProgram->getLayout();
    }
    return result;
}

GPU_TEST_CASE("link-time-constant", ALL)
{
    ComPtr<IShaderProgram> shaderProgram;
    slang::ProgramLayout* slangReflection = nullptr;
    REQUIRE_CALL(loadTestProgram(
        device,
        "test-link-time-constant",
        "computeMain",
        R"(
            export static const uint numthread = 4;
            export static const bool constBool = true;
            export static const int constInt = -2;
            export static const uint constUint = 3;
            export static const float constFloat = 4.0;
        )",
        shaderProgram.writeRef(),
        &slangReflection
    ));

    SlangUInt threadGroupSizes[3];
    slangReflection->findEntryPointByName("computeMain")->getComputeThreadGroupSize(3, threadGroupSizes);
    CHECK_EQ(threadGroupSizes[0], 4);
    CHECK_EQ(threadGroupSizes[1], 1);
    CHECK_EQ(threadGroupSizes[2], 1);

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

    compareComputeResult(device, buffer, makeArray<float>(1.f, -2.f, 3.f, 4.f));
}
