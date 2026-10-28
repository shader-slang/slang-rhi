#include "testing.h"
#include "../src/state-tracking.h"

using namespace rhi;
using namespace rhi::testing;

struct Shader
{
    ComPtr<IShaderProgram> program;
    slang::ProgramLayout* reflection = nullptr;
    ComputePipelineDesc pipelineDesc = {};
    ComPtr<IComputePipeline> pipeline;
};

ComPtr<IBuffer> createFloatBuffer(
    IDevice* device,
    bool unorderedAccess,
    size_t elementCount,
    float* initialData = nullptr
)
{
    BufferDesc bufferDesc = {};
    bufferDesc.size = elementCount * sizeof(float);
    bufferDesc.format = Format::Undefined;
    bufferDesc.elementSize = sizeof(float);
    bufferDesc.memoryType = MemoryType::DeviceLocal;
    bufferDesc.usage = BufferUsage::ShaderResource | BufferUsage::CopyDestination | BufferUsage::CopySource;
    if (unorderedAccess)
        bufferDesc.usage |= BufferUsage::UnorderedAccess;
    bufferDesc.defaultState = unorderedAccess ? ResourceState::UnorderedAccess : ResourceState::ShaderResource;
    ComPtr<IBuffer> buffer;
    REQUIRE_CALL(device->createBuffer(bufferDesc, (void*)initialData, buffer.writeRef()));
    return buffer;
}

GPU_TEST_CASE("buffer-barrier", ALL)
{
    Shader programA;
    Shader programB;
    REQUIRE_CALL(
        loadAndLinkProgram(device, "test-buffer-barrier", "computeA", programA.program.writeRef(), &programA.reflection)
    );
    REQUIRE_CALL(
        loadAndLinkProgram(device, "test-buffer-barrier", "computeB", programB.program.writeRef(), &programB.reflection)
    );
    programA.pipelineDesc.program = programA.program.get();
    programB.pipelineDesc.program = programB.program.get();
    REQUIRE_CALL(device->createComputePipeline(programA.pipelineDesc, programA.pipeline.writeRef()));
    REQUIRE_CALL(device->createComputePipeline(programB.pipelineDesc, programB.pipeline.writeRef()));

    float initialData[] = {1.0f, 2.0f, 3.0f, 4.0f};
    ComPtr<IBuffer> inputBuffer = createFloatBuffer(device, false, 4, initialData);
    ComPtr<IBuffer> intermediateBuffer = createFloatBuffer(device, true, 4);
    ComPtr<IBuffer> outputBuffer = createFloatBuffer(device, true, 4);

    // We have done all the set up work, now it is time to start recording a command buffer for
    // GPU execution.
    {
        auto queue = device->getQueue(QueueType::Graphics);
        auto commandEncoder = queue->createCommandEncoder();

        // Write inputBuffer to intermediateBuffer
        {
            auto passEncoder = commandEncoder->beginComputePass();
            auto rootObject = passEncoder->bindPipeline(programA.pipeline);
            ShaderCursor cursor(rootObject->getEntryPoint(0));
            cursor["inBuffer"].setBinding(inputBuffer);
            cursor["outBuffer"].setBinding(intermediateBuffer);
            passEncoder->dispatchCompute(1, 1, 1);
            passEncoder->end();
        }

        // Resource transition is automatically handled.

        // Write intermediateBuffer to outputBuffer
        {
            auto passEncoder = commandEncoder->beginComputePass();
            auto rootObject = passEncoder->bindPipeline(programB.pipeline);
            ShaderCursor cursor(rootObject->getEntryPoint(0));
            cursor["inBuffer"].setBinding(intermediateBuffer);
            cursor["outBuffer"].setBinding(outputBuffer);
            passEncoder->dispatchCompute(1, 1, 1);
            passEncoder->end();
        }

        queue->submit(commandEncoder->finish());
        queue->waitOnHost();
    }

    compareComputeResult(device, outputBuffer, makeArray<float>(11.0f, 12.0f, 13.0f, 14.0f));
}

// TODO: Currently disabled because the race condition will not ALWAYS materialize, making the test unreliable.
#if 0
GPU_TEST_CASE("buffer-no-barrier-race-condition", ALL)
{
    Shader programA;
    Shader programB;
    REQUIRE_CALL(loadAndLinkProgram(device, "test-buffer-barrier", "computeA", programA.program, &programA.reflection));
    REQUIRE_CALL(loadAndLinkProgram(device, "test-buffer-barrier", "computeB", programB.program, &programB.reflection));
    programA.pipelineDesc.program = programA.program.get();
    programB.pipelineDesc.program = programB.program.get();
    REQUIRE_CALL(device->createComputePipeline(programA.pipelineDesc, programA.pipeline.writeRef()));
    REQUIRE_CALL(device->createComputePipeline(programB.pipelineDesc, programB.pipeline.writeRef()));

    float initialData[] = {1.0f, 2.0f, 3.0f, 4.0f};
    ComPtr<IBuffer> inputBuffer = createFloatBuffer(device, false, 4, initialData);
    ComPtr<IBuffer> intermediateBuffer = createFloatBuffer(device, true, 4);
    ComPtr<IBuffer> outputBuffer = createFloatBuffer(device, true, 4);

    // We have done all the set up work, now it is time to start recording a command buffer for
    // GPU execution.
    {
        auto queue = device->getQueue(QueueType::Graphics);
        auto commandEncoder = queue->createCommandEncoder();

        // Write inputBuffer to intermediateBuffer
        {
            auto passEncoder = commandEncoder->beginComputePass();
            auto rootObject = passEncoder->bindPipeline(programA.pipeline);
            ShaderCursor cursor(rootObject->getEntryPoint(0));
            cursor["inBuffer"].setBinding(inputBuffer);
            cursor["outBuffer"].setBinding(intermediateBuffer);
            passEncoder->dispatchCompute(1, 1, 1);
            passEncoder->end();
        }

        // Write intermediateBuffer to outputBuffer
        {
            auto passEncoder = commandEncoder->beginComputePass();
            auto rootObject = passEncoder->bindPipeline(programB.pipeline);
            ShaderCursor cursor(rootObject->getEntryPoint(0));
            cursor["inBuffer"].setBinding(intermediateBuffer);
            cursor["outBuffer"].setBinding(outputBuffer);
            passEncoder->dispatchCompute(1, 1, 1);
            passEncoder->end();
        }

        // Disable state tracking for the submit
        testing::gDebugDisableStateTracking = true;
        queue->submit(commandEncoder->finish());
        testing::gDebugDisableStateTracking = false;
        queue->waitOnHost();
    }

    // We expect the 2 platforms that do explicit state tracking normally to fail,
    // as we disabled it for the submit.
    bool expectFailure = device->getDeviceType() == DeviceType::D3D12 || device->getDeviceType() == DeviceType::Vulkan;
    compareComputeResult(device, outputBuffer, makeArray<float>(11.0f, 12.0f, 13.0f, 14.0f), expectFailure);
}
#endif

GPU_TEST_CASE("buffer-global-barrier", D3D12 | Vulkan)
{
    Shader programA;
    Shader programB;
    REQUIRE_CALL(
        loadAndLinkProgram(device, "test-buffer-barrier", "computeA", programA.program.writeRef(), &programA.reflection)
    );
    REQUIRE_CALL(
        loadAndLinkProgram(device, "test-buffer-barrier", "computeB", programB.program.writeRef(), &programB.reflection)
    );
    programA.pipelineDesc.program = programA.program.get();
    programB.pipelineDesc.program = programB.program.get();
    REQUIRE_CALL(device->createComputePipeline(programA.pipelineDesc, programA.pipeline.writeRef()));
    REQUIRE_CALL(device->createComputePipeline(programB.pipelineDesc, programB.pipeline.writeRef()));

    float initialData[] = {1.0f, 2.0f, 3.0f, 4.0f};
    ComPtr<IBuffer> inputBuffer = createFloatBuffer(device, false, 4, initialData);
    ComPtr<IBuffer> intermediateBuffer = createFloatBuffer(device, true, 4);
    ComPtr<IBuffer> outputBuffer = createFloatBuffer(device, true, 4);

    // We have done all the set up work, now it is time to start recording a command buffer for
    // GPU execution.
    {
        auto queue = device->getQueue(QueueType::Graphics);
        auto commandEncoder = queue->createCommandEncoder();

        // Write inputBuffer to intermediateBuffer
        {
            auto passEncoder = commandEncoder->beginComputePass();
            auto rootObject = passEncoder->bindPipeline(programA.pipeline);
            ShaderCursor cursor(rootObject->getEntryPoint(0));
            cursor["inBuffer"].setBinding(inputBuffer);
            cursor["outBuffer"].setBinding(intermediateBuffer);
            passEncoder->dispatchCompute(1, 1, 1);
            passEncoder->end();
        }

        // Explicitly add a global barrier to the encoder, ensuring all
        // previous memory operations are visibile before starting the next
        // pass.
        commandEncoder->globalBarrier();

        // Write intermediateBuffer to outputBuffer
        {
            auto passEncoder = commandEncoder->beginComputePass();
            auto rootObject = passEncoder->bindPipeline(programB.pipeline);
            ShaderCursor cursor(rootObject->getEntryPoint(0));
            cursor["inBuffer"].setBinding(intermediateBuffer);
            cursor["outBuffer"].setBinding(outputBuffer);
            passEncoder->dispatchCompute(1, 1, 1);
            passEncoder->end();
        }

        // Disable state tracking for the submit
        testing::gDebugDisableStateTracking = true;
        queue->submit(commandEncoder->finish());
        testing::gDebugDisableStateTracking = false;
        queue->waitOnHost();
    }

    compareComputeResult(device, outputBuffer, makeArray<float>(11.0f, 12.0f, 13.0f, 14.0f));
}
