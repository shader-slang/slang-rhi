#include "testing.h"

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
    bufferDesc.format = Format::Unknown;
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

void testBufferBarrier(GpuTestContext* ctx, DeviceType deviceType)
{
    ComPtr<IDevice> device = createTestingDevice(ctx, deviceType);

    Shader programA;
    Shader programB;
    REQUIRE_CALL(loadComputeProgram(device, programA.program, "test-buffer-barrier", "computeA", programA.reflection));
    REQUIRE_CALL(loadComputeProgram(device, programB.program, "test-buffer-barrier", "computeB", programB.reflection));
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
        auto encoder = queue->createCommandEncoder();

        // Write inputBuffer to intermediateBuffer
        {
            auto rootObject = device->createRootShaderObject(programA.pipeline);
            ShaderCursor cursor(rootObject->getEntryPoint(0));
            cursor["inBuffer"].setBinding(inputBuffer);
            cursor["outBuffer"].setBinding(intermediateBuffer);
            rootObject->finalize();

            encoder->beginComputePass();
            ComputeState state;
            state.pipeline = programA.pipeline;
            state.rootObject = rootObject;
            encoder->setComputeState(state);
            encoder->dispatchCompute(1, 1, 1);
            encoder->endComputePass();
        }

        // Resource transition is automatically handled.

        // Write intermediateBuffer to outputBuffer
        {
            auto rootObject = device->createRootShaderObject(programB.pipeline);
            ShaderCursor cursor(rootObject->getEntryPoint(0));
            cursor["inBuffer"].setBinding(intermediateBuffer);
            cursor["outBuffer"].setBinding(outputBuffer);
            rootObject->finalize();

            encoder->beginComputePass();
            ComputeState state;
            state.pipeline = programB.pipeline;
            state.rootObject = rootObject;
            encoder->setComputeState(state);
            encoder->dispatchCompute(1, 1, 1);
            encoder->endComputePass();
        }

        queue->submit(encoder->finish());
        queue->waitOnHost();
    }

    compareComputeResult(device, outputBuffer, makeArray<float>(11.0f, 12.0f, 13.0f, 14.0f));
}

TEST_CASE("buffer-barrier")
{
    // D3D11 and Metal don't work
    runGpuTests(
        testBufferBarrier,
        {
            DeviceType::D3D12,
            DeviceType::Vulkan,
            DeviceType::CUDA,
            DeviceType::CPU,
            DeviceType::WGPU,
        }
    );
}
