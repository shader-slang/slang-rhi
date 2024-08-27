#include "testing.h"

using namespace rhi;
using namespace rhi::testing;

struct Shader
{
    ComPtr<IShaderProgram> program;
    slang::ProgramLayout* reflection = nullptr;
    ComputePipelineStateDesc pipelineDesc = {};
    ComPtr<IPipelineState> pipelineState;
};

struct Buffer
{
    IBufferResource::Desc desc;
    ComPtr<IBufferResource> buffer;
    ComPtr<IResourceView> view;
};

void createFloatBuffer(IDevice* device, Buffer& outBuffer, bool unorderedAccess, float* initialData, size_t elementCount)
{
    outBuffer = {};
    IBufferResource::Desc& bufferDesc = outBuffer.desc;
    bufferDesc.sizeInBytes = elementCount * sizeof(float);
    bufferDesc.format = Format::Unknown;
    bufferDesc.elementSize = sizeof(float);
    bufferDesc.defaultState = unorderedAccess ? ResourceState::UnorderedAccess : ResourceState::ShaderResource;
    bufferDesc.memoryType = MemoryType::DeviceLocal;
    bufferDesc.allowedStates = ResourceStateSet(
        ResourceState::ShaderResource,
        ResourceState::CopyDestination,
        ResourceState::CopySource);
    if (unorderedAccess) bufferDesc.allowedStates.add(ResourceState::UnorderedAccess);

    REQUIRE_CALL(device->createBufferResource(
        bufferDesc,
        (void*)initialData,
        outBuffer.buffer.writeRef()));

    IResourceView::Desc viewDesc = {};
    viewDesc.type = unorderedAccess ? IResourceView::Type::UnorderedAccess : IResourceView::Type::ShaderResource;
    viewDesc.format = Format::Unknown;
    REQUIRE_CALL(device->createBufferView(
        outBuffer.buffer, nullptr, viewDesc, outBuffer.view.writeRef()));
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
    REQUIRE_CALL(device->createComputePipelineState(programA.pipelineDesc, programA.pipelineState.writeRef()));
    REQUIRE_CALL(device->createComputePipelineState(programB.pipelineDesc, programB.pipelineState.writeRef()));

    float initialData[] = { 1.0f, 2.0f, 3.0f, 4.0f };
    Buffer inputBuffer;
    createFloatBuffer(device, inputBuffer, false, initialData, 4);

    Buffer intermediateBuffer;
    createFloatBuffer(device, intermediateBuffer, true, nullptr, 4);

    Buffer outputBuffer;
    createFloatBuffer(device, outputBuffer, true, nullptr, 4);

    // We have done all the set up work, now it is time to start recording a command buffer for
    // GPU execution.
    {
        ICommandQueue::Desc queueDesc = { ICommandQueue::QueueType::Graphics };
        auto queue = device->createCommandQueue(queueDesc);

        auto commandBuffer = transientHeap->createCommandBuffer();
        auto encoder = commandBuffer->encodeComputeCommands();
        auto resourceEncoder = commandBuffer->encodeResourceCommands();

        // Write inputBuffer data to intermediateBuffer
        auto rootObjectA = encoder->bindPipeline(programA.pipelineState);
        ShaderCursor entryPointCursorA(rootObjectA->getEntryPoint(0));
        entryPointCursorA.getPath("inBuffer").setResource(inputBuffer.view);
        entryPointCursorA.getPath("outBuffer").setResource(intermediateBuffer.view);

        encoder->dispatchCompute(1, 1, 1);

        // Insert barrier to ensure writes to intermediateBuffer are complete before the next shader starts executing
        auto bufferPtr = intermediateBuffer.buffer.get();
        resourceEncoder->bufferBarrier(1, &bufferPtr, ResourceState::UnorderedAccess, ResourceState::ShaderResource);
        resourceEncoder->endEncoding();

        // Write intermediateBuffer to outputBuffer
        auto rootObjectB = encoder->bindPipeline(programB.pipelineState);
        ShaderCursor entryPointCursorB(rootObjectB->getEntryPoint(0));
        entryPointCursorB.getPath("inBuffer").setResource(intermediateBuffer.view);
        entryPointCursorB.getPath("outBuffer").setResource(outputBuffer.view);

        encoder->dispatchCompute(1, 1, 1);
        encoder->endEncoding();
        commandBuffer->close();
        queue->executeCommandBuffer(commandBuffer);
        queue->waitOnHost();
    }

    compareComputeResult(
        device,
        outputBuffer.buffer,
        makeArray<float>(11.0f, 12.0f, 13.0f, 14.0f));
}

TEST_CASE("buffer-barrier")
{
    runGpuTests(testBufferBarrier, {DeviceType::D3D12, DeviceType::Vulkan});
}
