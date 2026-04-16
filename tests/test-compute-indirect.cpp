#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

// Test dispatchComputeIndirect with a simple compute shader.
// The test sets up an indirect argument buffer with dispatch dimensions written by the GPU,
// then verifies the compute shader ran with the correct number of threads.
GPU_TEST_CASE("compute-indirect", D3D12 | Vulkan | CUDA)
{
    ComPtr<IShaderProgram> program;
    REQUIRE_CALL(loadProgram(device, "test-compute-indirect", "computeMain", program.writeRef()));

    ComPtr<IShaderProgram> writeArgsProgram;
    REQUIRE_CALL(loadProgram(device, "test-compute-indirect", "writeDispatchArgs", writeArgsProgram.writeRef()));

    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = program.get();
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

    ComputePipelineDesc writeArgsPipelineDesc = {};
    writeArgsPipelineDesc.program = writeArgsProgram.get();
    ComPtr<IComputePipeline> writeArgsPipeline;
    REQUIRE_CALL(device->createComputePipeline(writeArgsPipelineDesc, writeArgsPipeline.writeRef()));

    // Create output buffer to count how many threads ran for each dispatch
    const int dispatchCount = 4;
    BufferDesc outputBufferDesc = {};
    outputBufferDesc.size = dispatchCount * sizeof(uint32_t);
    outputBufferDesc.format = Format::Undefined;
    outputBufferDesc.elementSize = sizeof(uint32_t);
    outputBufferDesc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess | BufferUsage::CopyDestination |
                             BufferUsage::CopySource;
    outputBufferDesc.defaultState = ResourceState::UnorderedAccess;
    outputBufferDesc.memoryType = MemoryType::DeviceLocal;

    uint32_t initialData[dispatchCount] = {0, 0, 0, 0};
    ComPtr<IBuffer> outputBuffer;
    REQUIRE_CALL(device->createBuffer(outputBufferDesc, initialData, outputBuffer.writeRef()));

    // Create indirect argument buffer (uninitialized - will be written by GPU)
    // Each IndirectDispatchArguments contains (threadGroupCountX, threadGroupCountY, threadGroupCountZ)
    // With numthreads(4, 4, 1), each group has 16 threads
    BufferDesc indirectBufferDesc = {};
    indirectBufferDesc.size = dispatchCount * sizeof(IndirectDispatchArguments);
    indirectBufferDesc.elementSize = sizeof(IndirectDispatchArguments);
    indirectBufferDesc.usage =
        BufferUsage::IndirectArgument | BufferUsage::UnorderedAccess | BufferUsage::CopyDestination;
    indirectBufferDesc.defaultState = ResourceState::UnorderedAccess;
    indirectBufferDesc.memoryType = MemoryType::DeviceLocal;

    ComPtr<IBuffer> indirectBuffer;
    REQUIRE_CALL(device->createBuffer(indirectBufferDesc, nullptr, indirectBuffer.writeRef()));

    // Dispatch arguments to write: (x, y, z) thread group counts
    // dispatch0: 1x1x1 = 1 group = 16 threads
    // dispatch1: 2x1x1 = 2 groups = 32 threads
    // dispatch2: 1x2x1 = 2 groups = 32 threads
    // dispatch3: 2x2x1 = 4 groups = 64 threads
    uint32_t threadGroupCounts[dispatchCount][3] = {
        {1, 1, 1},
        {2, 1, 1},
        {1, 2, 1},
        {2, 2, 1},
    };

    // Execute: first write dispatch args on GPU, then run indirect dispatches
    {
        auto queue = device->getQueue(QueueType::Graphics);
        auto commandEncoder = queue->createCommandEncoder();

        // First pass: write dispatch arguments using GPU
        {
            auto passEncoder = commandEncoder->beginComputePass();
            for (int i = 0; i < dispatchCount; i++)
            {
                auto rootObject = passEncoder->bindPipeline(writeArgsPipeline);
                ShaderCursor shaderCursor(rootObject);
                shaderCursor["dispatchArgsBuffer"].setBinding(indirectBuffer);
                uint32_t dispatchIndex = i;
                shaderCursor["dispatchIndex"].setData(dispatchIndex);
                uint32_t counts[3] = {threadGroupCounts[i][0], threadGroupCounts[i][1], threadGroupCounts[i][2]};
                shaderCursor["threadGroupCounts"].setData(counts, sizeof(counts));
                passEncoder->dispatchCompute(1, 1, 1);
            }
            passEncoder->end();
        }

        // Second pass: execute indirect dispatches
        {
            auto passEncoder = commandEncoder->beginComputePass();
            for (int i = 0; i < dispatchCount; i++)
            {
                auto rootObject = passEncoder->bindPipeline(pipeline);
                ShaderCursor shaderCursor(rootObject);
                shaderCursor["outputBuffer"].setBinding(outputBuffer);
                uint32_t dispatchIndex = i;
                shaderCursor["dispatchIndex"].setData(dispatchIndex);
                passEncoder->dispatchComputeIndirect({indirectBuffer, i * sizeof(IndirectDispatchArguments)});
            }
            passEncoder->end();
        }

        queue->submit(commandEncoder->finish());
        queue->waitOnHost();
    }

    // Verify results
    // With numthreads(4, 4, 1) = 16 threads per group:
    // dispatch0: 1x1x1 = 1 group = 16 threads
    // dispatch1: 2x1x1 = 2 groups = 32 threads
    // dispatch2: 1x2x1 = 2 groups = 32 threads
    // dispatch3: 2x2x1 = 4 groups = 64 threads
    compareComputeResult(device, outputBuffer, makeArray<uint32_t>(16, 32, 32, 64));
}

// Test dispatchComputeIndirect with zero dispatch dimensions.
// This verifies the implementation handles edge cases correctly.
GPU_TEST_CASE("compute-indirect-zero", D3D12 | Vulkan | CUDA)
{
    ComPtr<IShaderProgram> program;
    REQUIRE_CALL(loadProgram(device, "test-compute-indirect", "computeMain", program.writeRef()));

    ComPtr<IShaderProgram> writeArgsProgram;
    REQUIRE_CALL(loadProgram(device, "test-compute-indirect", "writeDispatchArgs", writeArgsProgram.writeRef()));

    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = program.get();
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

    ComputePipelineDesc writeArgsPipelineDesc = {};
    writeArgsPipelineDesc.program = writeArgsProgram.get();
    ComPtr<IComputePipeline> writeArgsPipeline;
    REQUIRE_CALL(device->createComputePipeline(writeArgsPipelineDesc, writeArgsPipeline.writeRef()));

    // Create output buffer
    BufferDesc outputBufferDesc = {};
    outputBufferDesc.size = sizeof(uint32_t);
    outputBufferDesc.format = Format::Undefined;
    outputBufferDesc.elementSize = sizeof(uint32_t);
    outputBufferDesc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess | BufferUsage::CopyDestination |
                             BufferUsage::CopySource;
    outputBufferDesc.defaultState = ResourceState::UnorderedAccess;
    outputBufferDesc.memoryType = MemoryType::DeviceLocal;

    uint32_t initialData = 0;
    ComPtr<IBuffer> outputBuffer;
    REQUIRE_CALL(device->createBuffer(outputBufferDesc, &initialData, outputBuffer.writeRef()));

    // Create indirect argument buffer (will be written by GPU with zero dispatch)
    BufferDesc indirectBufferDesc = {};
    indirectBufferDesc.size = sizeof(IndirectDispatchArguments);
    indirectBufferDesc.elementSize = sizeof(IndirectDispatchArguments);
    indirectBufferDesc.usage =
        BufferUsage::IndirectArgument | BufferUsage::UnorderedAccess | BufferUsage::CopyDestination;
    indirectBufferDesc.defaultState = ResourceState::UnorderedAccess;
    indirectBufferDesc.memoryType = MemoryType::DeviceLocal;

    ComPtr<IBuffer> indirectBuffer;
    REQUIRE_CALL(device->createBuffer(indirectBufferDesc, nullptr, indirectBuffer.writeRef()));

    // Execute: write zero dispatch args, then run indirect dispatch
    {
        auto queue = device->getQueue(QueueType::Graphics);
        auto commandEncoder = queue->createCommandEncoder();

        // First pass: write zero dispatch arguments using GPU
        {
            auto passEncoder = commandEncoder->beginComputePass();
            auto rootObject = passEncoder->bindPipeline(writeArgsPipeline);
            ShaderCursor shaderCursor(rootObject);
            shaderCursor["dispatchArgsBuffer"].setBinding(indirectBuffer);
            uint32_t dispatchIndex = 0;
            shaderCursor["dispatchIndex"].setData(dispatchIndex);
            uint32_t counts[3] = {0, 0, 0};
            shaderCursor["threadGroupCounts"].setData(counts, sizeof(counts));
            passEncoder->dispatchCompute(1, 1, 1);
            passEncoder->end();
        }

        // Second pass: execute indirect dispatch with zero dimensions
        {
            auto passEncoder = commandEncoder->beginComputePass();
            auto rootObject = passEncoder->bindPipeline(pipeline);
            ShaderCursor shaderCursor(rootObject);
            shaderCursor["outputBuffer"].setBinding(outputBuffer);
            uint32_t dispatchIndex = 0;
            shaderCursor["dispatchIndex"].setData(dispatchIndex);
            passEncoder->dispatchComputeIndirect({indirectBuffer, 0});
            passEncoder->end();
        }

        queue->submit(commandEncoder->finish());
        queue->waitOnHost();
    }

    // Verify no threads ran (output should still be 0)
    compareComputeResult(device, outputBuffer, makeArray<uint32_t>(0));
}

// Test dispatchComputeIndirect with non-zero buffer offset.
// This verifies the offset parameter is handled correctly.
GPU_TEST_CASE("compute-indirect-offset", D3D12 | Vulkan | CUDA)
{
    ComPtr<IShaderProgram> program;
    REQUIRE_CALL(loadProgram(device, "test-compute-indirect", "computeMain", program.writeRef()));

    ComPtr<IShaderProgram> writeArgsProgram;
    REQUIRE_CALL(loadProgram(device, "test-compute-indirect", "writeDispatchArgs", writeArgsProgram.writeRef()));

    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = program.get();
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

    ComputePipelineDesc writeArgsPipelineDesc = {};
    writeArgsPipelineDesc.program = writeArgsProgram.get();
    ComPtr<IComputePipeline> writeArgsPipeline;
    REQUIRE_CALL(device->createComputePipeline(writeArgsPipelineDesc, writeArgsPipeline.writeRef()));

    // Create output buffer
    BufferDesc outputBufferDesc = {};
    outputBufferDesc.size = sizeof(uint32_t);
    outputBufferDesc.format = Format::Undefined;
    outputBufferDesc.elementSize = sizeof(uint32_t);
    outputBufferDesc.usage = BufferUsage::ShaderResource | BufferUsage::UnorderedAccess | BufferUsage::CopyDestination |
                             BufferUsage::CopySource;
    outputBufferDesc.defaultState = ResourceState::UnorderedAccess;
    outputBufferDesc.memoryType = MemoryType::DeviceLocal;

    uint32_t initialData = 0;
    ComPtr<IBuffer> outputBuffer;
    REQUIRE_CALL(device->createBuffer(outputBufferDesc, &initialData, outputBuffer.writeRef()));

    // Create indirect argument buffer with space for 2 dispatch args (index 0 unused, index 1 used)
    // This tests that the offset parameter works correctly
    BufferDesc indirectBufferDesc = {};
    indirectBufferDesc.size = 2 * sizeof(IndirectDispatchArguments);
    indirectBufferDesc.elementSize = sizeof(IndirectDispatchArguments);
    indirectBufferDesc.usage =
        BufferUsage::IndirectArgument | BufferUsage::UnorderedAccess | BufferUsage::CopyDestination;
    indirectBufferDesc.defaultState = ResourceState::UnorderedAccess;
    indirectBufferDesc.memoryType = MemoryType::DeviceLocal;

    ComPtr<IBuffer> indirectBuffer;
    REQUIRE_CALL(device->createBuffer(indirectBufferDesc, nullptr, indirectBuffer.writeRef()));

    // Execute: write dispatch args at index 1, then dispatch using offset
    {
        auto queue = device->getQueue(QueueType::Graphics);
        auto commandEncoder = queue->createCommandEncoder();

        // First pass: write dispatch arguments at index 1 using GPU
        {
            auto passEncoder = commandEncoder->beginComputePass();
            auto rootObject = passEncoder->bindPipeline(writeArgsPipeline);
            ShaderCursor shaderCursor(rootObject);
            shaderCursor["dispatchArgsBuffer"].setBinding(indirectBuffer);
            uint32_t dispatchIndex = 1; // Write to second slot
            shaderCursor["dispatchIndex"].setData(dispatchIndex);
            uint32_t counts[3] = {3, 1, 1}; // 3x1x1 = 3 groups = 48 threads
            shaderCursor["threadGroupCounts"].setData(counts, sizeof(counts));
            passEncoder->dispatchCompute(1, 1, 1);
            passEncoder->end();
        }

        // Second pass: execute indirect dispatch with offset to second slot
        {
            auto passEncoder = commandEncoder->beginComputePass();
            auto rootObject = passEncoder->bindPipeline(pipeline);
            ShaderCursor shaderCursor(rootObject);
            shaderCursor["outputBuffer"].setBinding(outputBuffer);
            uint32_t dispatchIndex = 0;
            shaderCursor["dispatchIndex"].setData(dispatchIndex);
            // Use offset to point to second dispatch args slot
            passEncoder->dispatchComputeIndirect({indirectBuffer, sizeof(IndirectDispatchArguments)});
            passEncoder->end();
        }

        queue->submit(commandEncoder->finish());
        queue->waitOnHost();
    }

    // Verify correct number of threads ran (3 groups * 16 threads = 48)
    compareComputeResult(device, outputBuffer, makeArray<uint32_t>(48));
}
