#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

GPU_TEST_CASE("compute-trivial", ALL)
{
    ComPtr<IShaderProgram> shaderProgram;
    REQUIRE_CALL(loadProgram(device, "test-compute-trivial", "computeMain", shaderProgram.writeRef()));

    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

    const int numberCount = 4;
    float initialData[] = {0.0f, 1.0f, 2.0f, 3.0f};
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
        ShaderCursor shaderCursor(rootObject);
        shaderCursor["buffer"].setBinding(buffer);
        float value = 10.f;
        shaderCursor["value"].setData(value);
        passEncoder->dispatchCompute(1, 1, 1);
        passEncoder->end();

        queue->submit(commandEncoder->finish());
        queue->waitOnHost();
    }

    compareComputeResult(device, buffer, makeArray<float>(11.0f, 12.0f, 13.0f, 14.0f));
}

// A program whose entry point was renamed with IEntryPoint::renameEntryPoint must run like any other.
// The compiled kernel is emitted under the new name, while reflection's getName() still reports the
// name in the source.
GPU_TEST_CASE("compute-renamed-entry-point", ALL)
{
    const char* source = R"(
        RWStructuredBuffer<uint> result;
        [shader("compute")]
        [numthreads(1, 1, 1)]
        void computeMain(uint3 tid: SV_DispatchThreadID)
        {
            result[0] = 1234;
        }
    )";

    auto slangSession = device->getSlangSession();
    ComPtr<slang::IBlob> diagnostics;
    slang::IModule* module = slangSession->loadModuleFromSourceString(
        "compute-renamed-entry-point",
        "compute-renamed-entry-point.slang",
        source,
        diagnostics.writeRef()
    );
    diagnoseIfNeeded(diagnostics);
    REQUIRE(module != nullptr);

    ComPtr<slang::IEntryPoint> entryPoint;
    REQUIRE_CALL(module->findEntryPointByName("computeMain", entryPoint.writeRef()));
    ComPtr<slang::IComponentType> renamedEntryPoint;
    REQUIRE_CALL(entryPoint->renameEntryPoint("renamedMain", renamedEntryPoint.writeRef()));

    slang::IComponentType* components[] = {module, renamedEntryPoint.get()};
    ComPtr<slang::IComponentType> composedProgram;
    REQUIRE_CALL(
        slangSession->createCompositeComponentType(components, 2, composedProgram.writeRef(), diagnostics.writeRef())
    );
    diagnoseIfNeeded(diagnostics);
    ComPtr<slang::IComponentType> linkedProgram;
    REQUIRE_CALL(composedProgram->link(linkedProgram.writeRef(), diagnostics.writeRef()));
    diagnoseIfNeeded(diagnostics);

    ShaderProgramDesc programDesc = {};
    programDesc.slangGlobalScope = linkedProgram;
    ComPtr<IShaderProgram> shaderProgram;
    REQUIRE_CALL(device->createShaderProgram(programDesc, shaderProgram.writeRef(), diagnostics.writeRef()));
    diagnoseIfNeeded(diagnostics);

    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

    BufferDesc bufferDesc = {};
    bufferDesc.size = sizeof(uint32_t);
    bufferDesc.elementSize = sizeof(uint32_t);
    bufferDesc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess | BufferUsage::CopySource;
    bufferDesc.defaultState = ResourceState::UnorderedAccess;
    bufferDesc.memoryType = MemoryType::DeviceLocal;
    uint32_t initialData = 0;
    ComPtr<IBuffer> buffer;
    REQUIRE_CALL(device->createBuffer(bufferDesc, &initialData, buffer.writeRef()));

    {
        auto queue = device->getQueue(QueueType::Graphics);
        auto commandEncoder = queue->createCommandEncoder();
        auto passEncoder = commandEncoder->beginComputePass();
        auto rootObject = passEncoder->bindPipeline(pipeline);
        ShaderCursor(rootObject)["result"].setBinding(buffer);
        passEncoder->dispatchCompute(1, 1, 1);
        passEncoder->end();
        queue->submit(commandEncoder->finish());
        queue->waitOnHost();
    }

    compareComputeResult(device, buffer, makeArray<uint32_t>(1234u));
}
