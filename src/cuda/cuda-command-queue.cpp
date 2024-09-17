#include "cuda-command-queue.h"
#include "cuda-buffer.h"
#include "cuda-command-buffer.h"
#include "cuda-query.h"
#include "cuda-shader-object-layout.h"

namespace rhi::cuda {

ICommandQueue* CommandQueueImpl::getInterface(const Guid& guid)
{
    if (guid == GUID::IID_ISlangUnknown || guid == GUID::IID_ICommandQueue)
        return static_cast<ICommandQueue*>(this);
    return nullptr;
}

void CommandQueueImpl::init(DeviceImpl* inRenderer)
{
    renderer = inRenderer;
    m_desc.type = ICommandQueue::QueueType::Graphics;
    cuStreamCreate(&stream, 0);
}

CommandQueueImpl::~CommandQueueImpl()
{
    cuStreamSynchronize(stream);
    cuStreamDestroy(stream);
    currentPipeline = nullptr;
    currentRootObject = nullptr;
}

void CommandQueueImpl::executeCommandBuffers(
    GfxCount count,
    ICommandBuffer* const* commandBuffers,
    IFence* fence,
    uint64_t valueToSignal
)
{
    SLANG_UNUSED(valueToSignal);
    // TODO: implement fence.
    SLANG_RHI_ASSERT(fence == nullptr);
    for (GfxIndex i = 0; i < count; i++)
    {
        execute(static_cast<CommandBufferImpl*>(commandBuffers[i]));
    }
}

void CommandQueueImpl::waitOnHost()
{
    auto resultCode = cuStreamSynchronize(stream);
    if (resultCode != CUDA_SUCCESS)
        SLANG_CUDA_HANDLE_ERROR(resultCode);
}

Result CommandQueueImpl::waitForFenceValuesOnDevice(GfxCount fenceCount, IFence** fences, uint64_t* waitValues)
{
    return SLANG_FAIL;
}

Result CommandQueueImpl::getNativeHandle(NativeHandle* outHandle)
{
    *outHandle = {};
    return SLANG_E_NOT_AVAILABLE;
}

void CommandQueueImpl::setPipeline(IPipeline* state)
{
    currentPipeline = dynamic_cast<ComputePipelineImpl*>(state);
}

Result CommandQueueImpl::bindRootShaderObject(IShaderObject* object)
{
    currentRootObject = dynamic_cast<RootShaderObjectImpl*>(object);
    if (currentRootObject)
        return SLANG_OK;
    return SLANG_E_INVALID_ARG;
}

void CommandQueueImpl::dispatchCompute(int x, int y, int z)
{
    // Specialize the compute kernel based on the shader object bindings.
    RefPtr<Pipeline> newPipeline;
    renderer->maybeSpecializePipeline(currentPipeline, currentRootObject, newPipeline);
    currentPipeline = static_cast<ComputePipelineImpl*>(newPipeline.Ptr());

    // Find out thread group size from program reflection.
    auto& kernelName = currentPipeline->shaderProgram->kernelName;
    auto programLayout = static_cast<RootShaderObjectLayoutImpl*>(currentRootObject->getLayout());
    int kernelId = programLayout->getKernelIndex(kernelName);
    SLANG_RHI_ASSERT(kernelId != -1);
    UInt threadGroupSize[3];
    programLayout->getKernelThreadGroupSize(kernelId, threadGroupSize);

    // Copy global parameter data to the `SLANG_globalParams` symbol.
    {
        CUdeviceptr globalParamsSymbol = 0;
        size_t globalParamsSymbolSize = 0;
        cuModuleGetGlobal(
            &globalParamsSymbol,
            &globalParamsSymbolSize,
            currentPipeline->shaderProgram->cudaModule,
            "SLANG_globalParams"
        );

        CUdeviceptr globalParamsCUDAData = (CUdeviceptr)currentRootObject->getBuffer();
        cuMemcpyAsync((CUdeviceptr)globalParamsSymbol, (CUdeviceptr)globalParamsCUDAData, globalParamsSymbolSize, 0);
    }
    //
    // The argument data for the entry-point parameters are already
    // stored in host memory in a CUDAEntryPointShaderObject, as expected by cuLaunchKernel.
    //
    auto entryPointBuffer = currentRootObject->entryPointObjects[kernelId]->getBuffer();
    auto entryPointDataSize = currentRootObject->entryPointObjects[kernelId]->getBufferSize();

    void* extraOptions[] = {
        CU_LAUNCH_PARAM_BUFFER_POINTER,
        entryPointBuffer,
        CU_LAUNCH_PARAM_BUFFER_SIZE,
        &entryPointDataSize,
        CU_LAUNCH_PARAM_END,
    };

    // Once we have all the necessary data extracted and/or
    // set up, we can launch the kernel and see what happens.
    //
    auto cudaLaunchResult = cuLaunchKernel(
        currentPipeline->shaderProgram->cudaKernel,
        x,
        y,
        z,
        int(threadGroupSize[0]),
        int(threadGroupSize[1]),
        int(threadGroupSize[2]),
        0,
        stream,
        nullptr,
        extraOptions
    );

    SLANG_RHI_ASSERT(cudaLaunchResult == CUDA_SUCCESS);
}

void CommandQueueImpl::copyBuffer(IBuffer* dst, size_t dstOffset, IBuffer* src, size_t srcOffset, size_t size)
{
    auto dstImpl = static_cast<BufferImpl*>(dst);
    auto srcImpl = static_cast<BufferImpl*>(src);
    cuMemcpy(
        (CUdeviceptr)((uint8_t*)dstImpl->m_cudaMemory + dstOffset),
        (CUdeviceptr)((uint8_t*)srcImpl->m_cudaMemory + srcOffset),
        size
    );
}

void CommandQueueImpl::uploadBufferData(IBuffer* dst, size_t offset, size_t size, void* data)
{
    auto dstImpl = static_cast<BufferImpl*>(dst);
    cuMemcpy((CUdeviceptr)((uint8_t*)dstImpl->m_cudaMemory + offset), (CUdeviceptr)data, size);
}

void CommandQueueImpl::writeTimestamp(IQueryPool* pool, SlangInt index)
{
    auto poolImpl = static_cast<QueryPoolImpl*>(pool);
    cuEventRecord(poolImpl->m_events[index], stream);
}

void CommandQueueImpl::execute(CommandBufferImpl* commandBuffer)
{
    for (auto& cmd : commandBuffer->m_commands)
    {
        switch (cmd.name)
        {
        case CommandName::SetPipeline:
            setPipeline(commandBuffer->getObject<Pipeline>(cmd.operands[0]));
            break;
        case CommandName::BindRootShaderObject:
            bindRootShaderObject(commandBuffer->getObject<ShaderObjectBase>(cmd.operands[0]));
            break;
        case CommandName::DispatchCompute:
            dispatchCompute(int(cmd.operands[0]), int(cmd.operands[1]), int(cmd.operands[2]));
            break;
        case CommandName::CopyBuffer:
            copyBuffer(
                commandBuffer->getObject<Buffer>(cmd.operands[0]),
                cmd.operands[1],
                commandBuffer->getObject<Buffer>(cmd.operands[2]),
                cmd.operands[3],
                cmd.operands[4]
            );
            break;
        case CommandName::UploadBufferData:
            uploadBufferData(
                commandBuffer->getObject<Buffer>(cmd.operands[0]),
                cmd.operands[1],
                cmd.operands[2],
                commandBuffer->getData<uint8_t>(cmd.operands[3])
            );
            break;
        case CommandName::WriteTimestamp:
            writeTimestamp(commandBuffer->getObject<QueryPool>(cmd.operands[0]), (SlangInt)cmd.operands[1]);
        }
    }
}

} // namespace rhi::cuda
