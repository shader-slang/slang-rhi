#include "testing.h"
#include <random>
#include "../src/core/span.h"

using namespace rhi;
using namespace rhi::testing;

// TODO Add Metal when slang bug fixed
GPU_TEST_CASE("bind-pointers", Vulkan | CUDA)
{
    ComPtr<IShaderProgram> shaderProgram;
    slang::ProgramLayout* slangReflection = nullptr;
    REQUIRE_CALL(loadComputeProgram(device, shaderProgram, "test-pointer-copy", "computeMain", slangReflection));

    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

    const int numberCount = 4096;

    // Generate random data for 'numberCount' uint32s
    std::vector<uint8_t> data;
    std::mt19937 rng(124112);
    std::uniform_int_distribution<int> dist(0, 255);
    data.resize(numberCount * 4);
    for (auto& byte : data)
        byte = (uint8_t)dist(rng);

    // Setup buffer descriptor
    BufferDesc bufferDesc = {};
    bufferDesc.size = numberCount * sizeof(uint32_t);
    bufferDesc.format = Format::Undefined;
    bufferDesc.elementSize = sizeof(uint32_t);
    bufferDesc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess | BufferUsage::CopyDestination |
                       BufferUsage::CopySource;
    bufferDesc.defaultState = ResourceState::UnorderedAccess;
    bufferDesc.memoryType = MemoryType::DeviceLocal;

    // Create source buffer
    ComPtr<IBuffer> src;
    REQUIRE_CALL(device->createBuffer(bufferDesc, (void*)data.data(), src.writeRef()));

    // Create empty dest buffer
    ComPtr<IBuffer> dst;
    REQUIRE_CALL(device->createBuffer(bufferDesc, nullptr, dst.writeRef()));


    // We have done all the set up work, now it is time to start recording a command buffer for
    // GPU execution.
    {
        auto queue = device->getQueue(QueueType::Graphics);
        auto commandEncoder = queue->createCommandEncoder();

        auto passEncoder = commandEncoder->beginComputePass();
        auto rootObject = passEncoder->bindPipeline(pipeline);
        ShaderCursor shaderCursor(rootObject);
        shaderCursor["src"].setData(src->getDeviceAddress());
        shaderCursor["dst"].setData(dst->getDeviceAddress());

        passEncoder->dispatchCompute(numberCount / 32, 1, 1);
        passEncoder->end();

        queue->submit(commandEncoder->finish());
        queue->waitOnHost();
    }

    compareComputeResult(device, dst, span<uint8_t>(data));
}
