#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

void testUint16StructuredBuffer(GpuTestContext* ctx, DeviceType deviceType)
{
    ComPtr<IDevice> device = createTestingDevice(ctx, deviceType);

    ComPtr<IShaderProgram> shaderProgram;
    slang::ProgramLayout* slangReflection;
    REQUIRE_CALL(loadComputeProgram(device, shaderProgram, "test-uint16-buffer", "computeMain", slangReflection));

    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

    const int numberCount = 4;
    uint16_t initialData[] = {0, 1, 2, 3};
    BufferDesc bufferDesc = {};
    bufferDesc.size = numberCount * sizeof(uint16_t);
    bufferDesc.format = Format::Unknown;
    // Note: we don't specify any element size here, and rhi should be able to derive the
    // correct element size from the reflection infomation.
    bufferDesc.elementSize = 0;
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
        auto encoder = queue->createCommandEncoder();

        auto rootObject = device->createRootShaderObject(pipeline);

        // Bind buffer view to the entry point.
        ShaderCursor(rootObject)["buffer"].setBinding(buffer);

        rootObject->finalize();

        encoder->beginComputePass();
        ComputeState state;
        state.pipeline = pipeline;
        state.rootObject = rootObject;
        encoder->setComputeState(state);
        encoder->dispatchCompute(1, 1, 1);
        encoder->endComputePass();

        queue->submit(encoder->finish());
        queue->waitOnHost();
    }

    compareComputeResult(device, buffer, makeArray<uint16_t>(1, 2, 3, 4));
}

TEST_CASE("uint16-structured-buffer")
{
    runGpuTests(
        testUint16StructuredBuffer,
        {
            // DeviceType::D3D11, // fxc doesn't support uint16_t
            DeviceType::D3D12,
            DeviceType::Vulkan,
            DeviceType::Metal,
            DeviceType::CPU,
            DeviceType::CUDA,
            // DeviceType::WGPU, // crashes
        }
    );
}
