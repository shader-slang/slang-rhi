#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

struct Shader
{
    ComPtr<IShaderProgram> program;
    slang::ProgramLayout* reflection = nullptr;
    ComputePipelineDesc pipelineDesc = {};
    ComPtr<IPipeline> pipeline;
};

struct Buffer
{
    BufferDesc desc;
    ComPtr<IBuffer> buffer;
    ComPtr<IResourceView> srv;
    ComPtr<IResourceView> uav;
};

void createFloatBuffer(
    IDevice* device,
    Buffer& outBuffer,
    bool unorderedAccess,
    float* initialData,
    size_t elementCount
)
{
    outBuffer = {};
    BufferDesc& bufferDesc = outBuffer.desc;
    bufferDesc.size = elementCount * sizeof(float);
    bufferDesc.format = Format::Unknown;
    bufferDesc.elementSize = sizeof(float);
    bufferDesc.defaultState = unorderedAccess ? ResourceState::UnorderedAccess : ResourceState::ShaderResource;
    bufferDesc.memoryType = MemoryType::DeviceLocal;
    bufferDesc.usage = BufferUsage::ShaderResource | BufferUsage::CopyDestination | BufferUsage::CopySource;
    if (unorderedAccess)
        bufferDesc.usage |= BufferUsage::UnorderedAccess;

    REQUIRE_CALL(device->createBuffer(bufferDesc, (void*)initialData, outBuffer.buffer.writeRef()));

    {
        IResourceView::Desc viewDesc = {};
        viewDesc.type = IResourceView::Type::ShaderResource;
        viewDesc.format = Format::Unknown;
        REQUIRE_CALL(device->createBufferView(outBuffer.buffer, nullptr, viewDesc, outBuffer.srv.writeRef()));
    }

    if (unorderedAccess)
    {
        IResourceView::Desc viewDesc = {};
        viewDesc.type = IResourceView::Type::UnorderedAccess;
        viewDesc.format = Format::Unknown;
        REQUIRE_CALL(device->createBufferView(outBuffer.buffer, nullptr, viewDesc, outBuffer.uav.writeRef()));
    }
}

void testBufferBarrier(GpuTestContext* ctx, DeviceType deviceType)
{
    ComPtr<IDevice> device = createTestingDevice(ctx, deviceType);

    ComPtr<ITransientResourceHeap> transientHeap;
    ITransientResourceHeap::Desc transientHeapDesc = {};
    transientHeapDesc.constantBufferSize = 4096;
    REQUIRE_CALL(device->createTransientResourceHeap(transientHeapDesc, transientHeap.writeRef()));

    Shader programA;
    Shader programB;
    REQUIRE_CALL(loadComputeProgram(device, programA.program, "test-buffer-barrier", "computeA", programA.reflection));
    REQUIRE_CALL(loadComputeProgram(device, programB.program, "test-buffer-barrier", "computeB", programB.reflection));
    programA.pipelineDesc.program = programA.program.get();
    programB.pipelineDesc.program = programB.program.get();
    REQUIRE_CALL(device->createComputePipeline(programA.pipelineDesc, programA.pipeline.writeRef()));
    REQUIRE_CALL(device->createComputePipeline(programB.pipelineDesc, programB.pipeline.writeRef()));

    float initialData[] = {1.0f, 2.0f, 3.0f, 4.0f};
    Buffer inputBuffer;
    createFloatBuffer(device, inputBuffer, false, initialData, 4);

    Buffer intermediateBuffer;
    createFloatBuffer(device, intermediateBuffer, true, nullptr, 4);

    Buffer outputBuffer;
    createFloatBuffer(device, outputBuffer, true, nullptr, 4);

    auto insertBarrier = [](ICommandEncoder* encoder, IBuffer* buffer, ResourceState before, ResourceState after)
    { encoder->bufferBarrier(1, &buffer, before, after); };

    // We have done all the set up work, now it is time to start recording a command buffer for
    // GPU execution.
    {
        ICommandQueue::Desc queueDesc = {ICommandQueue::QueueType::Graphics};
        auto queue = device->createCommandQueue(queueDesc);

        auto commandBuffer = transientHeap->createCommandBuffer();
        auto encoder = commandBuffer->encodeComputeCommands();

        // Write inputBuffer data to intermediateBuffer
        auto rootObjectA = encoder->bindPipeline(programA.pipeline);
        ShaderCursor entryPointCursorA(rootObjectA->getEntryPoint(0));
        entryPointCursorA.getPath("inBuffer").setResource(inputBuffer.srv);
        entryPointCursorA.getPath("outBuffer").setResource(intermediateBuffer.uav);

        encoder->dispatchCompute(1, 1, 1);

        // Insert barrier to ensure writes to intermediateBuffer are complete before the next shader starts executing
        insertBarrier(
            encoder,
            intermediateBuffer.buffer,
            ResourceState::UnorderedAccess,
            ResourceState::ShaderResource
        );

        // Write intermediateBuffer to outputBuffer
        auto rootObjectB = encoder->bindPipeline(programB.pipeline);
        ShaderCursor entryPointCursorB(rootObjectB->getEntryPoint(0));
        entryPointCursorB.getPath("inBuffer").setResource(intermediateBuffer.srv);
        entryPointCursorB.getPath("outBuffer").setResource(outputBuffer.uav);

        encoder->dispatchCompute(1, 1, 1);
        encoder->endEncoding();
        commandBuffer->close();
        queue->executeCommandBuffer(commandBuffer);
        queue->waitOnHost();
    }

    compareComputeResult(device, outputBuffer.buffer, makeArray<float>(11.0f, 12.0f, 13.0f, 14.0f));
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
        }
    );
}
