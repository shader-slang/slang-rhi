#include "testing.h"

#include "../src/command-buffer.h"
#include "../src/device.h"

#include <array>
#include <vector>

using namespace rhi;
using namespace rhi::testing;

namespace {

class ConstantBufferWorkload
{
public:
    void init(IDevice* device, uint32_t valueCount)
    {
        m_device = device;

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
        REQUIRE_CALL(device->createComputePipeline(pipelineDesc, m_pipeline.writeRef()));

        std::vector<uint32_t> initialData(valueCount, 0);
        BufferDesc bufferDesc = {};
        bufferDesc.size = initialData.size() * sizeof(uint32_t);
        bufferDesc.elementSize = sizeof(uint32_t);
        bufferDesc.usage = BufferUsage::UnorderedAccess | BufferUsage::CopyDestination | BufferUsage::CopySource;
        bufferDesc.defaultState = ResourceState::UnorderedAccess;
        bufferDesc.memoryType = MemoryType::DeviceLocal;
        REQUIRE_CALL(device->createBuffer(bufferDesc, initialData.data(), m_output.writeRef()));

        REQUIRE_CALL(device->getQueue(QueueType::Graphics, m_queue.writeRef()));
    }

    std::vector<ComPtr<ICommandBuffer>> record(uint32_t firstIndex, uint32_t count)
    {
        std::vector<ComPtr<ICommandBuffer>> commandBuffers;
        commandBuffers.reserve(count);

        const std::array<uint32_t, 256> padding = {};
        for (uint32_t i = 0; i < count; ++i)
        {
            const uint32_t index = firstIndex + i;
            const uint32_t value = index + 1;

            ComPtr<ICommandEncoder> commandEncoder = m_queue->createCommandEncoder();
            REQUIRE(commandEncoder);

            IComputePassEncoder* passEncoder = commandEncoder->beginComputePass();
            REQUIRE(passEncoder);

            IShaderObject* rootObject = passEncoder->bindPipeline(m_pipeline);
            REQUIRE(rootObject);

            ShaderCursor cursor(rootObject);
            REQUIRE_CALL(cursor["output"].setBinding(m_output));
            REQUIRE_CALL(cursor["index"].setData(index));
            REQUIRE_CALL(cursor["value"].setData(value));
            REQUIRE_CALL(cursor["padding"].setData(padding.data(), padding.size() * sizeof(uint32_t)));

            passEncoder->dispatchCompute(1, 1, 1);
            passEncoder->end();

            ComPtr<ICommandBuffer> commandBuffer = commandEncoder->finish();
            REQUIRE(commandBuffer);
            commandBuffers.push_back(commandBuffer);
        }

        return commandBuffers;
    }

    void submitAndWait(const std::vector<ComPtr<ICommandBuffer>>& commandBuffers)
    {
        for (const auto& commandBuffer : commandBuffers)
            REQUIRE_CALL(m_queue->submit(commandBuffer));
        REQUIRE_CALL(m_queue->waitOnHost());
    }

    void checkResult(uint32_t valueCount)
    {
        std::vector<uint32_t> expected(valueCount);
        for (uint32_t i = 0; i < valueCount; ++i)
            expected[i] = i + 1;
        compareComputeResult(m_device, m_output, std::span<uint32_t>(expected));
    }

    ICommandQueue* getQueue() const { return m_queue; }

private:
    IDevice* m_device = nullptr;
    ComPtr<IComputePipeline> m_pipeline;
    ComPtr<IBuffer> m_output;
    ComPtr<ICommandQueue> m_queue;
};

} // namespace

GPU_TEST_CASE("constant-buffer-multiple-command-buffers", ALL)
{
    // Keep this count modest: backends without a shared transient heap can allocate a full
    // constant-buffer page (or a staging/device page pair) for every command buffer.
    static constexpr uint32_t kCommandBufferCount = 4;

    ConstantBufferWorkload workload;
    workload.init(device, kCommandBufferCount);

    // Keep every command buffer alive until recording has completed so this exercises
    // simultaneous constant-buffer allocation lifetimes on every backend.
    auto commandBuffers = workload.record(0, kCommandBufferCount);
    workload.submitAndWait(commandBuffers);
    workload.checkResult(kCommandBufferCount);
}

GPU_TEST_CASE("transient-buffer-arena-shared-pages", D3D12 | Vulkan | DontCacheDevice)
{
    static constexpr uint32_t kSubmissionCount = 64;
    static constexpr uint32_t kWaveCount = 2;
    static constexpr uint32_t kValueCount = kSubmissionCount * kWaveCount;

    ConstantBufferWorkload workload;
    workload.init(device, kValueCount);

    // Reach through the debug layer only to inspect the internal heap statistics.
    ComPtr<ICommandQueue> innerQueue;
    REQUIRE_CALL(getUnderlyingDevice(device)->getQueue(QueueType::Graphics, innerQueue.writeRef()));
    TransientBufferHeap& heap = checked_cast<CommandQueue*>(innerQueue.get())->m_constantBufferHeap;

    FenceDesc fenceDesc = {};
    ComPtr<IFence> submissionGate;
    REQUIRE_CALL(device->createFence(fenceDesc, submissionGate.writeRef()));

    CHECK_EQ(heap.getNumPages(), 0);
    CHECK_EQ(heap.getCapacity(), 0);
    CHECK_EQ(heap.getUsed(), 0);
    CHECK_EQ(heap.getPageAllocationCount(), 0);
    CHECK_EQ(heap.getInitialPageSize(), 64 * 1024);
    CHECK_EQ(heap.getMaxPageSize(), 4 * 1024 * 1024);
    CHECK_EQ(heap.getMaxRetainedSize(), 4 * 1024 * 1024);

    uint64_t firstWavePageAllocationCount = 0;
    for (uint32_t wave = 0; wave < kWaveCount; ++wave)
    {
        // Record the complete wave before submitting anything. This guarantees that all
        // constant-buffer allocations are live together, independent of GPU execution speed.
        auto commandBuffers = workload.record(wave * kSubmissionCount, kSubmissionCount);

        // Many binding allocations collapse into a small number of coarse page leases.
        CHECK_GE(heap.getNumPages(), 2);
        CHECK_LT(heap.getNumPages(), kSubmissionCount);
        CHECK_GT(heap.getCapacity(), heap.getInitialPageSize());
        CHECK_GT(heap.getUsed(), heap.getInitialPageSize());

        const uint64_t pageAllocationCount = heap.getPageAllocationCount();
        if (wave == 0)
        {
            firstWavePageAllocationCount = pageAllocationCount;
        }
        else
        {
            // The second wave reuses retained pages without allocating new buffers.
            CHECK_EQ(pageAllocationCount, firstWavePageAllocationCount);
        }

        // Hold the queue's tracking signal behind an unsignaled fence. This makes the
        // fence-scoped allocation lifetime deterministic even when the GPU finishes quickly.
        const uint64_t submissionGateValue = wave + 1;
        for (const auto& commandBuffer : commandBuffers)
        {
            ICommandBuffer* commandBufferPtr = commandBuffer;
            IFence* waitFence = submissionGate;
            SubmitDesc submitDesc = {};
            submitDesc.commandBuffers = &commandBufferPtr;
            submitDesc.commandBufferCount = 1;
            submitDesc.waitFences = &waitFence;
            submitDesc.waitFenceValues = &submissionGateValue;
            submitDesc.waitFenceCount = 1;
            REQUIRE_CALL(workload.getQueue()->submit(submitDesc));
        }

        // Every submitted command buffer must still own its range until the fence can retire it.
        CHECK_GT(heap.getUsed(), 0);
        REQUIRE_CALL(submissionGate->setCurrentValue(submissionGateValue));

        REQUIRE_CALL(workload.getQueue()->waitOnHost());
        commandBuffers.clear();

        // Retirement releases all leases as one batch while retaining a bounded warm set.
        CHECK_EQ(heap.getUsed(), 0);
        CHECK_GT(heap.getNumPages(), 0);
        CHECK_LE(heap.getCapacity(), heap.getMaxRetainedSize());
        CHECK_EQ(heap.getPageAllocationCount(), pageAllocationCount);
    }

    workload.checkResult(kValueCount);
}
