#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

// Test dispatchComputeIndirect with a simple compute shader.
// The test sets up an indirect argument buffer with dispatch dimensions,
// then verifies the compute shader ran with the correct number of threads.
GPU_TEST_CASE("compute-indirect", D3D12 | Vulkan | CUDA)
{
    ComPtr<IShaderProgram> shaderProgram;
    REQUIRE_CALL(loadProgram(device, "test-compute-indirect", "computeMain", shaderProgram.writeRef()));

    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

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

    // Create indirect argument buffer with multiple dispatches
    // Each IndirectDispatchArguments contains (threadGroupCountX, threadGroupCountY, threadGroupCountZ)
    // With numthreads(4, 4, 1), each group has 16 threads
    struct IndirectArgs
    {
        IndirectDispatchArguments dispatch0; // 1x1x1 = 1 group = 16 threads
        IndirectDispatchArguments dispatch1; // 2x1x1 = 2 groups = 32 threads
        IndirectDispatchArguments dispatch2; // 1x2x1 = 2 groups = 32 threads
        IndirectDispatchArguments dispatch3; // 2x2x1 = 4 groups = 64 threads
    };

    IndirectArgs indirectArgs = {
        {1, 1, 1}, // dispatch0: 1 group
        {2, 1, 1}, // dispatch1: 2 groups
        {1, 2, 1}, // dispatch2: 2 groups
        {2, 2, 1}, // dispatch3: 4 groups
    };

    BufferDesc indirectBufferDesc = {};
    indirectBufferDesc.size = sizeof(IndirectArgs);
    indirectBufferDesc.usage = BufferUsage::IndirectArgument | BufferUsage::CopyDestination;
    indirectBufferDesc.defaultState = ResourceState::IndirectArgument;
    indirectBufferDesc.memoryType = MemoryType::DeviceLocal;

    ComPtr<IBuffer> indirectBuffer;
    REQUIRE_CALL(device->createBuffer(indirectBufferDesc, &indirectArgs, indirectBuffer.writeRef()));

    // Execute indirect dispatches
    {
        auto queue = device->getQueue(QueueType::Graphics);
        auto commandEncoder = queue->createCommandEncoder();

        auto passEncoder = commandEncoder->beginComputePass();

        // Dispatch 0: 1x1x1 groups
        {
            auto rootObject = passEncoder->bindPipeline(pipeline);
            ShaderCursor shaderCursor(rootObject->getEntryPoint(0));
            shaderCursor["outputBuffer"].setBinding(outputBuffer);
            uint32_t dispatchIndex = 0;
            shaderCursor["dispatchIndex"].setData(dispatchIndex);
            passEncoder->dispatchComputeIndirect({indirectBuffer, offsetof(IndirectArgs, dispatch0)});
        }

        // Dispatch 1: 2x1x1 groups
        {
            auto rootObject = passEncoder->bindPipeline(pipeline);
            ShaderCursor shaderCursor(rootObject->getEntryPoint(0));
            shaderCursor["outputBuffer"].setBinding(outputBuffer);
            uint32_t dispatchIndex = 1;
            shaderCursor["dispatchIndex"].setData(dispatchIndex);
            passEncoder->dispatchComputeIndirect({indirectBuffer, offsetof(IndirectArgs, dispatch1)});
        }

        // Dispatch 2: 1x2x1 groups
        {
            auto rootObject = passEncoder->bindPipeline(pipeline);
            ShaderCursor shaderCursor(rootObject->getEntryPoint(0));
            shaderCursor["outputBuffer"].setBinding(outputBuffer);
            uint32_t dispatchIndex = 2;
            shaderCursor["dispatchIndex"].setData(dispatchIndex);
            passEncoder->dispatchComputeIndirect({indirectBuffer, offsetof(IndirectArgs, dispatch2)});
        }

        // Dispatch 3: 2x2x1 groups
        {
            auto rootObject = passEncoder->bindPipeline(pipeline);
            ShaderCursor shaderCursor(rootObject->getEntryPoint(0));
            shaderCursor["outputBuffer"].setBinding(outputBuffer);
            uint32_t dispatchIndex = 3;
            shaderCursor["dispatchIndex"].setData(dispatchIndex);
            passEncoder->dispatchComputeIndirect({indirectBuffer, offsetof(IndirectArgs, dispatch3)});
        }

        passEncoder->end();

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
    ComPtr<IShaderProgram> shaderProgram;
    REQUIRE_CALL(loadProgram(device, "test-compute-indirect", "computeMain", shaderProgram.writeRef()));

    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

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

    // Create indirect argument buffer with zero dispatch
    IndirectDispatchArguments indirectArgs = {0, 0, 0};

    BufferDesc indirectBufferDesc = {};
    indirectBufferDesc.size = sizeof(IndirectDispatchArguments);
    indirectBufferDesc.usage = BufferUsage::IndirectArgument | BufferUsage::CopyDestination;
    indirectBufferDesc.defaultState = ResourceState::IndirectArgument;
    indirectBufferDesc.memoryType = MemoryType::DeviceLocal;

    ComPtr<IBuffer> indirectBuffer;
    REQUIRE_CALL(device->createBuffer(indirectBufferDesc, &indirectArgs, indirectBuffer.writeRef()));

    // Execute indirect dispatch with zero dimensions
    {
        auto queue = device->getQueue(QueueType::Graphics);
        auto commandEncoder = queue->createCommandEncoder();

        auto passEncoder = commandEncoder->beginComputePass();
        auto rootObject = passEncoder->bindPipeline(pipeline);
        ShaderCursor shaderCursor(rootObject->getEntryPoint(0));
        shaderCursor["outputBuffer"].setBinding(outputBuffer);
        uint32_t dispatchIndex = 0;
        shaderCursor["dispatchIndex"].setData(dispatchIndex);
        passEncoder->dispatchComputeIndirect({indirectBuffer, 0});
        passEncoder->end();

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
    ComPtr<IShaderProgram> shaderProgram;
    REQUIRE_CALL(loadProgram(device, "test-compute-indirect", "computeMain", shaderProgram.writeRef()));

    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram.get();
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

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

    // Create indirect argument buffer with padding before the actual arguments
    struct PaddedIndirectArgs
    {
        float padding[4];
        IndirectDispatchArguments dispatchArgs;
    };

    PaddedIndirectArgs indirectArgs = {
        {1.0f, 2.0f, 3.0f, 4.0f}, // padding (ignored)
        {3, 1, 1}                 // 3x1x1 = 3 groups = 48 threads
    };

    BufferDesc indirectBufferDesc = {};
    indirectBufferDesc.size = sizeof(PaddedIndirectArgs);
    indirectBufferDesc.usage = BufferUsage::IndirectArgument | BufferUsage::CopyDestination;
    indirectBufferDesc.defaultState = ResourceState::IndirectArgument;
    indirectBufferDesc.memoryType = MemoryType::DeviceLocal;

    ComPtr<IBuffer> indirectBuffer;
    REQUIRE_CALL(device->createBuffer(indirectBufferDesc, &indirectArgs, indirectBuffer.writeRef()));

    // Execute indirect dispatch with offset
    {
        auto queue = device->getQueue(QueueType::Graphics);
        auto commandEncoder = queue->createCommandEncoder();

        auto passEncoder = commandEncoder->beginComputePass();
        auto rootObject = passEncoder->bindPipeline(pipeline);
        ShaderCursor shaderCursor(rootObject->getEntryPoint(0));
        shaderCursor["outputBuffer"].setBinding(outputBuffer);
        uint32_t dispatchIndex = 0;
        shaderCursor["dispatchIndex"].setData(dispatchIndex);
        // Use offset to skip padding and point to actual dispatch args
        passEncoder->dispatchComputeIndirect({indirectBuffer, offsetof(PaddedIndirectArgs, dispatchArgs)});
        passEncoder->end();

        queue->submit(commandEncoder->finish());
        queue->waitOnHost();
    }

    // Verify correct number of threads ran (3 groups * 16 threads = 48)
    compareComputeResult(device, outputBuffer, makeArray<uint32_t>(48));
}
