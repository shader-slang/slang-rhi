#include "testing.h"

#include "../src/command-buffer.h"
#include "../src/device.h"

#include <array>
#include <vector>

using namespace rhi;
using namespace rhi::testing;

GPU_TEST_CASE("constant-buffer-pool-shared-pages", D3D12 | Vulkan | DontCacheDevice)
{
    static constexpr uint32_t kSubmissionCount = 64;
    static constexpr uint32_t kWaveCount = 2;
    static constexpr uint32_t kValueCount = kSubmissionCount * kWaveCount;

    ComPtr<IShaderProgram> shaderProgram;
    REQUIRE_CALL(loadComputeProgramFromSource(
        device,
        R"(
            uniform RWStructuredBuffer<uint> output;
            uniform uint index;
            uniform uint value;
            uniform uint4 padding[64];

            [shader("compute")]
            [numthreads(1, 1, 1)]
            void computeMain()
            {
                output[index] = value + padding[0].x;
            }
        )",
        shaderProgram.writeRef()
    ));

    ComputePipelineDesc pipelineDesc = {};
    pipelineDesc.program = shaderProgram;
    ComPtr<IComputePipeline> pipeline;
    REQUIRE_CALL(device->createComputePipeline(pipelineDesc, pipeline.writeRef()));

    std::vector<uint32_t> initialData(kValueCount, 0);
    BufferDesc bufferDesc = {};
    bufferDesc.size = initialData.size() * sizeof(uint32_t);
    bufferDesc.elementSize = sizeof(uint32_t);
    bufferDesc.usage = BufferUsage::UnorderedAccess | BufferUsage::CopyDestination | BufferUsage::CopySource;
    bufferDesc.defaultState = ResourceState::UnorderedAccess;
    bufferDesc.memoryType = MemoryType::DeviceLocal;

    ComPtr<IBuffer> output;
    REQUIRE_CALL(device->createBuffer(bufferDesc, initialData.data(), output.writeRef()));

    // Use the public queue for the workload so the debug layer remains active.
    ComPtr<ICommandQueue> queue;
    REQUIRE_CALL(device->getQueue(QueueType::Graphics, queue.writeRef()));

    // Reach through the debug layer only to inspect the internal heap statistics.
    ComPtr<ICommandQueue> innerQueue;
    REQUIRE_CALL(getUnderlyingDevice(device)->getQueue(QueueType::Graphics, innerQueue.writeRef()));
    StagingHeap& heap = checked_cast<CommandQueue*>(innerQueue.get())->m_constantBufferHeap;

    FenceDesc fenceDesc = {};
    ComPtr<IFence> submissionGate;
    REQUIRE_CALL(device->createFence(fenceDesc, submissionGate.writeRef()));

    CHECK_EQ(heap.getNumPages(), 0);
    CHECK_EQ(heap.getCapacity(), 0);
    CHECK_EQ(heap.getUsed(), 0);
    CHECK_EQ(heap.getPageAllocationCount(), 0);
    CHECK_EQ(heap.getPageSize(), 64 * 1024);
    CHECK_EQ(heap.getMaxPageSize(), 4 * 1024 * 1024);

    uint64_t firstWavePageAllocationCount = 0;
    const std::array<uint32_t, 256> padding = {};

    for (uint32_t wave = 0; wave < kWaveCount; ++wave)
    {
        std::vector<ComPtr<ICommandBuffer>> commandBuffers;
        commandBuffers.reserve(kSubmissionCount);

        // Record the complete wave before submitting anything. This guarantees that all
        // constant-buffer allocations are live together, independent of GPU execution speed.
        for (uint32_t i = 0; i < kSubmissionCount; ++i)
        {
            const uint32_t index = wave * kSubmissionCount + i;
            const uint32_t value = index + 1;

            ComPtr<ICommandEncoder> commandEncoder = queue->createCommandEncoder();
            REQUIRE(commandEncoder);

            IComputePassEncoder* passEncoder = commandEncoder->beginComputePass();
            REQUIRE(passEncoder);

            IShaderObject* rootObject = passEncoder->bindPipeline(pipeline);
            REQUIRE(rootObject);

            ShaderCursor cursor(rootObject);
            REQUIRE_CALL(cursor["output"].setBinding(output));
            REQUIRE_CALL(cursor["index"].setData(index));
            REQUIRE_CALL(cursor["value"].setData(value));
            REQUIRE_CALL(cursor["padding"].setData(padding.data(), padding.size() * sizeof(uint32_t)));

            passEncoder->dispatchCompute(1, 1, 1);
            passEncoder->end();

            ComPtr<ICommandBuffer> commandBuffer = commandEncoder->finish();
            REQUIRE(commandBuffer);
            commandBuffers.push_back(commandBuffer);
        }

        // The shared heap should grow from 64 KiB to 128 KiB for this wave.
        CHECK_EQ(heap.getNumPages(), 2);
        CHECK_EQ(heap.getCapacity(), heap.getPageSize() * 3);
        CHECK_GT(heap.getUsed(), heap.getPageSize());

        const uint64_t pageAllocationCount = heap.getPageAllocationCount();
        if (wave == 0)
        {
            CHECK_EQ(pageAllocationCount, 2);
            firstWavePageAllocationCount = pageAllocationCount;
        }
        else
        {
            // The initial page is reused and one 128 KiB page is regrown for the new wave.
            CHECK_EQ(pageAllocationCount, firstWavePageAllocationCount + 1);
        }

        // Hold the queue's tracking signal behind an unsignaled fence. This makes the
        // fence-scoped allocation lifetime deterministic even when the GPU finishes quickly.
        const uint64_t submissionGateValue = wave + 1;
        for (size_t i = 0; i < commandBuffers.size(); ++i)
        {
            ICommandBuffer* commandBuffer = commandBuffers[i].get();
            IFence* waitFence = submissionGate.get();
            SubmitDesc submitDesc = {};
            submitDesc.commandBuffers = &commandBuffer;
            submitDesc.commandBufferCount = 1;
            submitDesc.waitFences = &waitFence;
            submitDesc.waitFenceValues = &submissionGateValue;
            submitDesc.waitFenceCount = 1;
            REQUIRE_CALL(queue->submit(submitDesc));
        }

        // Every submitted command buffer must still own its range until the fence can retire it.
        CHECK_GT(heap.getUsed(), 0);
        REQUIRE_CALL(submissionGate->setCurrentValue(submissionGateValue));

        REQUIRE_CALL(queue->waitOnHost());
        commandBuffers.clear();

        // Retirement trims the grown page and keeps one warm 64 KiB page.
        CHECK_EQ(heap.getUsed(), 0);
        CHECK_EQ(heap.getNumPages(), 1);
        CHECK_EQ(heap.getCapacity(), heap.getPageSize());
        CHECK_EQ(heap.getPageAllocationCount(), pageAllocationCount);
    }

    std::vector<uint32_t> expected(kValueCount);
    for (uint32_t i = 0; i < kValueCount; ++i)
        expected[i] = i + 1;
    compareComputeResult(device, output, std::span<uint32_t>(expected));
}
