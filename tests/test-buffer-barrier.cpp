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

// A chain of dependent dispatches, one compute pass each, with no explicit barrier: each pass reads
// what the previous pass wrote, so every pass boundary must order the passes. Back-to-back compute
// passes are the case the Metal backend handles by keeping one compute encoder open and putting a
// memory barrier between the passes.
GPU_TEST_CASE("buffer-barrier-compute-pass-chain", ALL)
{
    Shader program;
    REQUIRE_CALL(loadAndLinkProgram(
        device,
        "test-buffer-barrier",
        "computeStep",
        program.program.writeRef(),
        &program.reflection
    ));
    program.pipelineDesc.program = program.program.get();
    REQUIRE_CALL(device->createComputePipeline(program.pipelineDesc, program.pipeline.writeRef()));

    const uint32_t count = 1024;
    const int stepCount = 64;
    std::vector<float> zeros(count, 0.0f);
    ComPtr<IBuffer> buffers[2] = {
        createFloatBuffer(device, true, count, zeros.data()),
        createFloatBuffer(device, true, count, zeros.data()),
    };

    auto queue = device->getQueue(QueueType::Graphics);
    auto commandEncoder = queue->createCommandEncoder();
    for (int step = 0; step < stepCount; ++step)
    {
        auto passEncoder = commandEncoder->beginComputePass();
        auto rootObject = passEncoder->bindPipeline(program.pipeline);
        ShaderCursor cursor(rootObject->getEntryPoint(0));
        cursor["count"].setData(count);
        cursor["inBuffer"].setBinding(buffers[step % 2]);
        cursor["outBuffer"].setBinding(buffers[(step + 1) % 2]);
        passEncoder->dispatchCompute((count + 63) / 64, 1, 1);
        passEncoder->end();
    }
    queue->submit(commandEncoder->finish());
    queue->waitOnHost();

    std::vector<float> expected(count, float(stepCount));
    compareComputeResult(device, buffers[stepCount % 2], std::span<float>(expected.data(), expected.size()));
}

// A compute pass, a copy, then a compute pass that reads the copy. On Metal the copy ends the
// compute encoder kept open after the first pass, so this covers the compute-to-blit-to-compute
// transition, which goes through the queue fence.
GPU_TEST_CASE("buffer-barrier-compute-copy-compute", ALL)
{
    Shader program;
    REQUIRE_CALL(loadAndLinkProgram(
        device,
        "test-buffer-barrier",
        "computeStep",
        program.program.writeRef(),
        &program.reflection
    ));
    program.pipelineDesc.program = program.program.get();
    REQUIRE_CALL(device->createComputePipeline(program.pipelineDesc, program.pipeline.writeRef()));

    const uint32_t count = 1024;
    std::vector<float> zeros(count, 0.0f);
    ComPtr<IBuffer> buffers[3] = {
        createFloatBuffer(device, true, count, zeros.data()),
        createFloatBuffer(device, true, count, zeros.data()),
        createFloatBuffer(device, true, count, zeros.data()),
    };

    auto queue = device->getQueue(QueueType::Graphics);
    auto commandEncoder = queue->createCommandEncoder();
    auto step = [&](IBuffer* in, IBuffer* out)
    {
        auto passEncoder = commandEncoder->beginComputePass();
        auto rootObject = passEncoder->bindPipeline(program.pipeline);
        ShaderCursor cursor(rootObject->getEntryPoint(0));
        cursor["count"].setData(count);
        cursor["inBuffer"].setBinding(in);
        cursor["outBuffer"].setBinding(out);
        passEncoder->dispatchCompute((count + 63) / 64, 1, 1);
        passEncoder->end();
    };
    step(buffers[0], buffers[1]);
    commandEncoder->copyBuffer(buffers[2], 0, buffers[1], 0, count * sizeof(float));
    step(buffers[2], buffers[0]);
    queue->submit(commandEncoder->finish());
    queue->waitOnHost();

    std::vector<float> expected(count, 2.0f);
    compareComputeResult(device, buffers[0], std::span<float>(expected.data(), expected.size()));
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
