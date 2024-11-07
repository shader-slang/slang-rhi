#include "wgpu-command.h"
#include "wgpu-device.h"
#include "wgpu-pipeline.h"
#include "wgpu-shader-object.h"
#include "wgpu-util.h"

#include "../strings.h"

#include "core/deferred.h"


namespace rhi::wgpu {

template<typename T>
inline bool arraysEqual(GfxCount countA, GfxCount countB, const T* a, const T* b)
{
    return (countA == countB) ? std::memcmp(a, b, countA * sizeof(T)) == 0 : false;
}

class CommandRecorder
{
public:
    DeviceImpl* m_device;
    Context& m_ctx;

    WGPUCommandEncoder m_commandEncoder = nullptr;
    WGPURenderPassEncoder m_renderPassEncoder = nullptr;
    WGPUComputePassEncoder m_computePassEncoder = nullptr;

    short_vector<RefPtr<TextureViewImpl>> m_renderTargetViews;
    short_vector<RefPtr<TextureViewImpl>> m_resolveTargetViews;
    RefPtr<TextureViewImpl> m_depthStencilView;

    bool m_renderPassActive = false;
    bool m_renderStateValid = false;
    RenderState m_renderState;
    RefPtr<RenderPipelineImpl> m_renderPipeline;

    bool m_computePassActive = false;
    bool m_computeStateValid = false;
    ComputeState m_computeState;
    RefPtr<ComputePipelineImpl> m_computePipeline;

    RefPtr<RootShaderObjectImpl> m_rootObject;

    CommandRecorder(DeviceImpl* device)
        : m_device(device)
        , m_ctx(device->m_ctx)
    {
    }

    Result record(CommandBufferImpl* commandBuffer);

    void cmdCopyBuffer(const commands::CopyBuffer& cmd);
    void cmdCopyTexture(const commands::CopyTexture& cmd);
    void cmdCopyTextureToBuffer(const commands::CopyTextureToBuffer& cmd);
    void cmdClearBuffer(const commands::ClearBuffer& cmd);
    void cmdClearTexture(const commands::ClearTexture& cmd);
    void cmdUploadTextureData(const commands::UploadTextureData& cmd);
    void cmdUploadBufferData(const commands::UploadBufferData& cmd);
    void cmdResolveQuery(const commands::ResolveQuery& cmd);
    void cmdBeginRenderPass(const commands::BeginRenderPass& cmd);
    void cmdEndRenderPass(const commands::EndRenderPass& cmd);
    void cmdSetRenderState(const commands::SetRenderState& cmd);
    void cmdDraw(const commands::Draw& cmd);
    void cmdDrawIndexed(const commands::DrawIndexed& cmd);
    void cmdDrawIndirect(const commands::DrawIndirect& cmd);
    void cmdDrawIndexedIndirect(const commands::DrawIndexedIndirect& cmd);
    void cmdDrawMeshTasks(const commands::DrawMeshTasks& cmd);
    void cmdBeginComputePass(const commands::BeginComputePass& cmd);
    void cmdEndComputePass(const commands::EndComputePass& cmd);
    void cmdSetComputeState(const commands::SetComputeState& cmd);
    void cmdDispatchCompute(const commands::DispatchCompute& cmd);
    void cmdDispatchComputeIndirect(const commands::DispatchComputeIndirect& cmd);
    void cmdBeginRayTracingPass(const commands::BeginRayTracingPass& cmd);
    void cmdEndRayTracingPass(const commands::EndRayTracingPass& cmd);
    void cmdSetRayTracingState(const commands::SetRayTracingState& cmd);
    void cmdDispatchRays(const commands::DispatchRays& cmd);
    void cmdBuildAccelerationStructure(const commands::BuildAccelerationStructure& cmd);
    void cmdCopyAccelerationStructure(const commands::CopyAccelerationStructure& cmd);
    void cmdQueryAccelerationStructureProperties(const commands::QueryAccelerationStructureProperties& cmd);
    void cmdSerializeAccelerationStructure(const commands::SerializeAccelerationStructure& cmd);
    void cmdDeserializeAccelerationStructure(const commands::DeserializeAccelerationStructure& cmd);
    void cmdSetBufferState(const commands::SetBufferState& cmd);
    void cmdSetTextureState(const commands::SetTextureState& cmd);
    void cmdBeginDebugEvent(const commands::BeginDebugEvent& cmd);
    void cmdEndDebugEvent(const commands::EndDebugEvent& cmd);
    void cmdWriteTimestamp(const commands::WriteTimestamp& cmd);
    void cmdExecuteCallback(const commands::ExecuteCallback& cmd);

    void endPassEncoder();
};

Result CommandRecorder::record(CommandBufferImpl* commandBuffer)
{
    m_commandEncoder = m_ctx.api.wgpuDeviceCreateCommandEncoder(m_ctx.device, nullptr);
    SLANG_RHI_DEFERRED({ m_ctx.api.wgpuCommandEncoderRelease(m_commandEncoder); });
    if (!m_commandEncoder)
    {
        return SLANG_FAIL;
    }

    CommandList* commandList = commandBuffer->m_commandList;
    auto command = commandList->getCommands();
    while (command)
    {
#define SLANG_RHI_COMMAND_EXECUTE_X(x)                                                                                 \
    case CommandID::x:                                                                                                 \
        cmd##x(commandList->getCommand<commands::x>(command));                                                         \
        break;

        switch (command->id)
        {
            SLANG_RHI_COMMANDS(SLANG_RHI_COMMAND_EXECUTE_X);
        }

#undef SLANG_RHI_COMMAND_EXECUTE_X

        command = command->next;
    }

    endPassEncoder();

    commandBuffer->m_commandBuffer = m_ctx.api.wgpuCommandEncoderFinish(m_commandEncoder, nullptr);
    if (!commandBuffer->m_commandBuffer)
    {
        return SLANG_FAIL;
    }

    return SLANG_OK;
}

#define NOT_SUPPORTED(x) m_device->warning(S_CommandEncoder_##x " command is not supported!")

void CommandRecorder::cmdCopyBuffer(const commands::CopyBuffer& cmd)
{
    BufferImpl* dst = checked_cast<BufferImpl*>(cmd.dst);
    BufferImpl* src = checked_cast<BufferImpl*>(cmd.src);
    m_ctx.api.wgpuCommandEncoderCopyBufferToBuffer(
        m_commandEncoder,
        src->m_buffer,
        cmd.srcOffset,
        dst->m_buffer,
        cmd.dstOffset,
        cmd.size
    );
}

void CommandRecorder::cmdCopyTexture(const commands::CopyTexture& cmd)
{
    TextureImpl* dst = checked_cast<TextureImpl*>(cmd.dst);
    TextureImpl* src = checked_cast<TextureImpl*>(cmd.src);

    WGPUImageCopyTexture source = {};
    source.texture = src->m_texture;
    source.origin = {(uint32_t)cmd.srcOffset.x, (uint32_t)cmd.srcOffset.y, (uint32_t)cmd.srcOffset.z};
    source.mipLevel = cmd.srcSubresource.mipLevel;
    source.aspect = WGPUTextureAspect_All;

    WGPUImageCopyTexture destination = {};
    destination.texture = dst->m_texture;
    destination.origin = {(uint32_t)cmd.dstOffset.x, (uint32_t)cmd.dstOffset.y, (uint32_t)cmd.dstOffset.z};
    destination.mipLevel = cmd.dstSubresource.mipLevel;
    destination.aspect = WGPUTextureAspect_All;

    WGPUExtent3D copySize = {(uint32_t)cmd.extent.width, (uint32_t)cmd.extent.height, (uint32_t)cmd.extent.depth};

    m_ctx.api.wgpuCommandEncoderCopyTextureToTexture(m_commandEncoder, &source, &destination, &copySize);
}

void CommandRecorder::cmdCopyTextureToBuffer(const commands::CopyTextureToBuffer& cmd)
{
    BufferImpl* dst = checked_cast<BufferImpl*>(cmd.dst);
    TextureImpl* src = checked_cast<TextureImpl*>(cmd.src);

    WGPUImageCopyTexture source = {};
    source.texture = src->m_texture;
    source.origin = {(uint32_t)cmd.srcOffset.x, (uint32_t)cmd.srcOffset.y, (uint32_t)cmd.srcOffset.z};
    source.mipLevel = cmd.srcSubresource.mipLevel;
    source.aspect = WGPUTextureAspect_All;

    WGPUImageCopyBuffer destination = {};
    destination.buffer = dst->m_buffer;
    destination.layout.offset = cmd.dstOffset;
    destination.layout.bytesPerRow = cmd.dstRowStride;
    destination.layout.rowsPerImage = src->m_desc.size.height >> cmd.srcSubresource.mipLevel;

    WGPUExtent3D copySize = {(uint32_t)cmd.extent.width, (uint32_t)cmd.extent.height, (uint32_t)cmd.extent.depth};

    m_ctx.api.wgpuCommandEncoderCopyTextureToBuffer(m_commandEncoder, &source, &destination, &copySize);
}

void CommandRecorder::cmdClearBuffer(const commands::ClearBuffer& cmd)
{
    BufferImpl* buffer = checked_cast<BufferImpl*>(cmd.buffer);
    m_ctx.api.wgpuCommandEncoderClearBuffer(m_commandEncoder, buffer->m_buffer, cmd.range.offset, cmd.range.size);
}

void CommandRecorder::cmdClearTexture(const commands::ClearTexture& cmd)
{
    NOT_SUPPORTED(clearTexture);
}

void CommandRecorder::cmdUploadTextureData(const commands::UploadTextureData& cmd)
{
    NOT_SUPPORTED(uploadTextureData);
}

void CommandRecorder::cmdUploadBufferData(const commands::UploadBufferData& cmd)
{
    NOT_SUPPORTED(uploadBufferData);
}

void CommandRecorder::cmdResolveQuery(const commands::ResolveQuery& cmd)
{
    NOT_SUPPORTED(resolveQuery);
}

void CommandRecorder::cmdBeginRenderPass(const commands::BeginRenderPass& cmd)
{
    const RenderPassDesc& desc = cmd.desc;

    short_vector<WGPURenderPassColorAttachment, 8> colorAttachments(desc.colorAttachmentCount, {});
    for (GfxIndex i = 0; i < desc.colorAttachmentCount; ++i)
    {
        const RenderPassColorAttachment& attachmentIn = desc.colorAttachments[i];
        WGPURenderPassColorAttachment& attachment = colorAttachments[i];
        attachment.view = checked_cast<TextureViewImpl*>(attachmentIn.view)->m_textureView;
        attachment.resolveTarget = attachmentIn.resolveTarget
                                       ? checked_cast<TextureViewImpl*>(attachmentIn.resolveTarget)->m_textureView
                                       : nullptr;
        attachment.depthSlice = -1;         // TODO not provided
        attachment.resolveTarget = nullptr; // TODO not provided
        attachment.loadOp = translateLoadOp(attachmentIn.loadOp);
        attachment.storeOp = translateStoreOp(attachmentIn.storeOp);
        attachment.clearValue.r = attachmentIn.clearValue[0];
        attachment.clearValue.g = attachmentIn.clearValue[1];
        attachment.clearValue.b = attachmentIn.clearValue[2];
        attachment.clearValue.a = attachmentIn.clearValue[3];
    }

    WGPURenderPassDepthStencilAttachment depthStencilAttachment = {};
    if (desc.depthStencilAttachment)
    {
        const RenderPassDepthStencilAttachment& attachmentIn = *desc.depthStencilAttachment;
        WGPURenderPassDepthStencilAttachment& attachment = depthStencilAttachment;
        attachment.view = checked_cast<TextureViewImpl*>(attachmentIn.view)->m_textureView;
        attachment.depthLoadOp = translateLoadOp(attachmentIn.depthLoadOp);
        attachment.depthStoreOp = translateStoreOp(attachmentIn.depthStoreOp);
        attachment.depthClearValue = attachmentIn.depthClearValue;
        attachment.depthReadOnly = attachmentIn.depthReadOnly;
        attachment.stencilLoadOp = translateLoadOp(attachmentIn.stencilLoadOp);
        attachment.stencilStoreOp = translateStoreOp(attachmentIn.stencilStoreOp);
        attachment.stencilClearValue = attachmentIn.stencilClearValue;
        attachment.stencilReadOnly = attachmentIn.stencilReadOnly;
    }

    WGPURenderPassDescriptor passDesc = {};
    passDesc.colorAttachmentCount = desc.colorAttachmentCount;
    passDesc.colorAttachments = colorAttachments.data();
    passDesc.depthStencilAttachment = desc.depthStencilAttachment ? &depthStencilAttachment : nullptr;
    // passDesc.occlusionQuerySet not supported
    // passDesc.timestampWrites not supported

    endPassEncoder();
    m_renderPassEncoder = m_ctx.api.wgpuCommandEncoderBeginRenderPass(m_commandEncoder, &passDesc);
}

void CommandRecorder::cmdEndRenderPass(const commands::EndRenderPass& cmd)
{
    endPassEncoder();
}

void CommandRecorder::cmdSetRenderState(const commands::SetRenderState& cmd)
{
    if (!m_renderPassEncoder)
        return;

    const RenderState& state = cmd.state;

    bool updatePipeline = !m_renderStateValid || state.pipeline != m_renderState.pipeline;
    bool updateRootObject = updatePipeline || state.rootObject != m_renderState.rootObject;
    bool updateStencilRef = !m_renderStateValid || state.stencilRef != m_renderState.stencilRef;
    bool updateVertexBuffers = !m_renderStateValid || !arraysEqual(
                                                          state.vertexBufferCount,
                                                          m_renderState.vertexBufferCount,
                                                          state.vertexBuffers,
                                                          m_renderState.vertexBuffers
                                                      );
    bool updateIndexBuffer = !m_renderStateValid || state.indexFormat != m_renderState.indexFormat ||
                             state.indexBuffer != m_renderState.indexBuffer;
    bool updateViewports =
        !m_renderStateValid ||
        !arraysEqual(state.viewportCount, m_renderState.viewportCount, state.viewports, m_renderState.viewports);
    bool updateScissorRects = !m_renderStateValid || !arraysEqual(
                                                         state.scissorRectCount,
                                                         m_renderState.scissorRectCount,
                                                         state.scissorRects,
                                                         m_renderState.scissorRects
                                                     );

    WGPURenderPassEncoder encoder = m_renderPassEncoder;

    if (updatePipeline)
    {
        m_renderPipeline = checked_cast<RenderPipelineImpl*>(state.pipeline);
        m_ctx.api.wgpuRenderPassEncoderSetPipeline(encoder, m_renderPipeline->m_renderPipeline);
    }

    if (updateRootObject)
    {
        m_rootObject = checked_cast<RootShaderObjectImpl*>(state.rootObject);
        auto specializedLayout = m_rootObject->getSpecializedLayout();
        BindingContext bindingContext;
        bindingContext.device = m_device;
        bindingContext.bindGroupLayouts = specializedLayout->m_bindGroupLayouts;
        m_rootObject->bindAsRoot(bindingContext, m_renderPipeline->m_rootObjectLayout);
        for (uint32_t groupIndex = 0; groupIndex < bindingContext.bindGroups.size(); groupIndex++)
        {
            m_ctx.api.wgpuRenderPassEncoderSetBindGroup(
                m_renderPassEncoder,
                groupIndex,
                bindingContext.bindGroups[groupIndex],
                0,
                nullptr
            );
        }
    }

    if (updateStencilRef)
    {
        m_ctx.api.wgpuRenderPassEncoderSetStencilReference(m_renderPassEncoder, state.stencilRef);
    }

    if (updateVertexBuffers)
    {
        for (Index i = 0; i < state.vertexBufferCount; ++i)
        {
            BufferImpl* buffer = checked_cast<BufferImpl*>(state.vertexBuffers[i].buffer);
            Offset offset = state.vertexBuffers[i].offset;
            m_ctx.api.wgpuRenderPassEncoderSetVertexBuffer(
                m_renderPassEncoder,
                i,
                buffer->m_buffer,
                offset,
                buffer->m_desc.size - offset
            );
        }
    }

    if (updateIndexBuffer)
    {
        if (state.indexBuffer.buffer)
        {
            BufferImpl* buffer = checked_cast<BufferImpl*>(state.indexBuffer.buffer);
            Offset offset = state.indexBuffer.offset;
            m_ctx.api.wgpuRenderPassEncoderSetIndexBuffer(
                m_renderPassEncoder,
                buffer->m_buffer,
                state.indexFormat == IndexFormat::UInt32 ? WGPUIndexFormat_Uint32 : WGPUIndexFormat_Uint16,
                offset,
                buffer->m_desc.size - offset
            );
        }
    }

    if (updateViewports && state.viewportCount > 0)
    {
        const Viewport& viewport = state.viewports[0];
        m_ctx.api.wgpuRenderPassEncoderSetViewport(
            m_renderPassEncoder,
            viewport.originX,
            viewport.originY,
            viewport.extentX,
            viewport.extentY,
            viewport.minZ,
            viewport.maxZ
        );
    }

    if (updateScissorRects && state.scissorRectCount > 0)
    {
        const ScissorRect& scissorRect = state.scissorRects[0];
        m_ctx.api.wgpuRenderPassEncoderSetScissorRect(
            m_renderPassEncoder,
            scissorRect.minX,
            scissorRect.minY,
            scissorRect.maxX - scissorRect.minX,
            scissorRect.maxY - scissorRect.minY
        );
    }

    m_renderStateValid = true;
    m_renderState = state;

    m_computeStateValid = false;
    m_computeState = {};
    m_computePipeline = nullptr;
}

void CommandRecorder::cmdDraw(const commands::Draw& cmd)
{
    if (!m_renderStateValid)
        return;

    m_ctx.api.wgpuRenderPassEncoderDraw(
        m_renderPassEncoder,
        cmd.args.vertexCount,
        cmd.args.instanceCount,
        cmd.args.startVertexLocation,
        cmd.args.startInstanceLocation
    );
}

void CommandRecorder::cmdDrawIndexed(const commands::DrawIndexed& cmd)
{
    if (!m_renderStateValid)
        return;

    m_ctx.api.wgpuRenderPassEncoderDrawIndexed(
        m_renderPassEncoder,
        cmd.args.vertexCount,
        cmd.args.instanceCount,
        cmd.args.startIndexLocation,
        cmd.args.startVertexLocation,
        cmd.args.startInstanceLocation
    );
}

void CommandRecorder::cmdDrawIndirect(const commands::DrawIndirect& cmd)
{
    if (!m_renderStateValid)
        return;

    m_ctx.api.wgpuRenderPassEncoderMultiDrawIndirect(
        m_renderPassEncoder,
        checked_cast<BufferImpl*>(cmd.argBuffer)->m_buffer,
        cmd.argOffset,
        cmd.maxDrawCount,
        cmd.countBuffer ? checked_cast<BufferImpl*>(cmd.countBuffer)->m_buffer : nullptr,
        cmd.countOffset
    );
}

void CommandRecorder::cmdDrawIndexedIndirect(const commands::DrawIndexedIndirect& cmd)
{
    if (!m_renderStateValid)
        return;

    m_ctx.api.wgpuRenderPassEncoderMultiDrawIndexedIndirect(
        m_renderPassEncoder,
        checked_cast<BufferImpl*>(cmd.argBuffer)->m_buffer,
        cmd.argOffset,
        cmd.maxDrawCount,
        cmd.countBuffer ? checked_cast<BufferImpl*>(cmd.countBuffer)->m_buffer : nullptr,
        cmd.countOffset
    );
}

void CommandRecorder::cmdDrawMeshTasks(const commands::DrawMeshTasks& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(drawMeshTasks);
}

void CommandRecorder::cmdBeginComputePass(const commands::BeginComputePass& cmd)
{
    m_computePassActive = true;
}

void CommandRecorder::cmdEndComputePass(const commands::EndComputePass& cmd)
{
    m_computePassActive = false;
}

void CommandRecorder::cmdSetComputeState(const commands::SetComputeState& cmd)
{
    if (!m_computePassActive)
        return;

    const ComputeState& state = cmd.state;

    bool updatePipeline = !m_computeStateValid || state.pipeline != m_computeState.pipeline;
    bool updateRootObject = updatePipeline || state.rootObject != m_computeState.rootObject;

    if (!m_computePassEncoder)
    {
        m_computePassEncoder = m_ctx.api.wgpuCommandEncoderBeginComputePass(m_commandEncoder, nullptr);
    }

    WGPUComputePassEncoder encoder = m_computePassEncoder;

    if (updatePipeline)
    {
        m_computePipeline = checked_cast<ComputePipelineImpl*>(state.pipeline);
        m_ctx.api.wgpuComputePassEncoderSetPipeline(encoder, m_computePipeline->m_computePipeline);
    }

    if (updateRootObject)
    {
        m_rootObject = checked_cast<RootShaderObjectImpl*>(state.rootObject);
        auto specializedLayout = m_rootObject->getSpecializedLayout();
        BindingContext bindingContext;
        bindingContext.device = m_device;
        bindingContext.bindGroupLayouts = specializedLayout->m_bindGroupLayouts;
        m_rootObject->bindAsRoot(bindingContext, specializedLayout);
        for (uint32_t groupIndex = 0; groupIndex < bindingContext.bindGroups.size(); groupIndex++)
        {
            m_ctx.api.wgpuComputePassEncoderSetBindGroup(
                m_computePassEncoder,
                groupIndex,
                bindingContext.bindGroups[groupIndex],
                0,
                nullptr
            );
        }
    }

    m_computeStateValid = true;
    m_computeState = state;
}

void CommandRecorder::cmdDispatchCompute(const commands::DispatchCompute& cmd)
{
    if (!m_computeStateValid)
        return;

    m_ctx.api.wgpuComputePassEncoderDispatchWorkgroups(m_computePassEncoder, cmd.x, cmd.y, cmd.z);
}

void CommandRecorder::cmdDispatchComputeIndirect(const commands::DispatchComputeIndirect& cmd)
{
    if (!m_computeStateValid)
        return;

    m_ctx.api.wgpuComputePassEncoderDispatchWorkgroupsIndirect(
        m_computePassEncoder,
        checked_cast<BufferImpl*>(cmd.argBuffer)->m_buffer,
        cmd.offset
    );
}

void CommandRecorder::cmdBeginRayTracingPass(const commands::BeginRayTracingPass& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(beginRayTracingPass);
}

void CommandRecorder::cmdEndRayTracingPass(const commands::EndRayTracingPass& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(endRayTracingPass);
}

void CommandRecorder::cmdSetRayTracingState(const commands::SetRayTracingState& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(setRayTracingState);
}

void CommandRecorder::cmdDispatchRays(const commands::DispatchRays& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(dispatchRays);
}

void CommandRecorder::cmdBuildAccelerationStructure(const commands::BuildAccelerationStructure& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(buildAccelerationStructure);
}

void CommandRecorder::cmdCopyAccelerationStructure(const commands::CopyAccelerationStructure& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(copyAccelerationStructure);
}

void CommandRecorder::cmdQueryAccelerationStructureProperties(const commands::QueryAccelerationStructureProperties& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(queryAccelerationStructureProperties);
}

void CommandRecorder::cmdSerializeAccelerationStructure(const commands::SerializeAccelerationStructure& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(serializeAccelerationStructure);
}

void CommandRecorder::cmdDeserializeAccelerationStructure(const commands::DeserializeAccelerationStructure& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(deserializeAccelerationStructure);
}

void CommandRecorder::cmdSetBufferState(const commands::SetBufferState& cmd)
{
    SLANG_UNUSED(cmd);
}

void CommandRecorder::cmdSetTextureState(const commands::SetTextureState& cmd)
{
    SLANG_UNUSED(cmd);
}

void CommandRecorder::cmdBeginDebugEvent(const commands::BeginDebugEvent& cmd)
{
    m_ctx.api.wgpuCommandEncoderPushDebugGroup(m_commandEncoder, cmd.name);
}

void CommandRecorder::cmdEndDebugEvent(const commands::EndDebugEvent& cmd)
{
    m_ctx.api.wgpuCommandEncoderPopDebugGroup(m_commandEncoder);
}

void CommandRecorder::cmdWriteTimestamp(const commands::WriteTimestamp& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(writeTimestamp);
}

void CommandRecorder::cmdExecuteCallback(const commands::ExecuteCallback& cmd)
{
    cmd.callback(cmd.userData);
}

void CommandRecorder::endPassEncoder()
{
    if (m_renderPassEncoder)
    {
        m_device->m_ctx.api.wgpuRenderPassEncoderEnd(m_renderPassEncoder);
        m_device->m_ctx.api.wgpuRenderPassEncoderRelease(m_renderPassEncoder);
        m_renderPassEncoder = nullptr;

        m_renderStateValid = false;
        m_renderState = {};
        m_renderPipeline = nullptr;
    }
    if (m_computePassEncoder)
    {
        m_device->m_ctx.api.wgpuComputePassEncoderEnd(m_computePassEncoder);
        m_device->m_ctx.api.wgpuComputePassEncoderRelease(m_computePassEncoder);
        m_computePassEncoder = nullptr;

        m_computeStateValid = false;
        m_computeState = {};
        m_computePipeline = nullptr;
    }

    m_rootObject = nullptr;
}

// CommandQueueImpl

CommandQueueImpl::CommandQueueImpl(DeviceImpl* device, QueueType type)
    : CommandQueue(device, type)
{
    m_queue = m_device->m_ctx.api.wgpuDeviceGetQueue(m_device->m_ctx.device);
}

CommandQueueImpl::~CommandQueueImpl()
{
    if (m_queue)
    {
        m_device->m_ctx.api.wgpuQueueRelease(m_queue);
    }
}

Result CommandQueueImpl::createCommandEncoder(ICommandEncoder** outEncoder)
{
    RefPtr<CommandEncoderImpl> encoder = new CommandEncoderImpl(m_device, this);
    returnComPtr(outEncoder, encoder);
    return SLANG_OK;
}

Result CommandQueueImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::WGPUQueue;
    outHandle->value = (uint64_t)m_queue;
    return SLANG_OK;
}

void CommandQueueImpl::waitOnHost()
{
    // Wait for the command buffer to finish executing
    // TODO: we should switch to the new async API
    {
        WGPUQueueWorkDoneStatus status = WGPUQueueWorkDoneStatus_Unknown;
        m_device->m_ctx.api.wgpuQueueOnSubmittedWorkDone(
            m_queue,
            [](WGPUQueueWorkDoneStatus status, void* userdata) { *(WGPUQueueWorkDoneStatus*)userdata = status; },
            &status
        );
        while (status == WGPUQueueWorkDoneStatus_Unknown)
        {
            m_device->m_ctx.api.wgpuDeviceTick(m_device->m_ctx.device);
        }
    }
}

Result CommandQueueImpl::waitForFenceValuesOnDevice(GfxCount fenceCount, IFence** fences, uint64_t* waitValues)
{
    // for (GfxCount i = 0; i < fenceCount; ++i)
    // {
    //     FenceWaitInfo waitInfo;
    //     waitInfo.fence = checked_cast<FenceImpl*>(fences[i]);
    //     waitInfo.waitValue = waitValues[i];
    //     m_pendingWaitFences.push_back(waitInfo);
    // }
    return SLANG_OK;
}

Result CommandQueueImpl::submit(
    GfxCount count,
    ICommandBuffer* const* commandBuffers,
    IFence* fence,
    uint64_t valueToSignal
)
{
    if (count == 0 && fence == nullptr)
    {
        return SLANG_OK;
    }

    short_vector<WGPUCommandBuffer, 16> buffers;
    buffers.resize(count);
    for (GfxIndex i = 0; i < count; ++i)
    {
        buffers[i] = checked_cast<CommandBufferImpl*>(commandBuffers[i])->m_commandBuffer;
    }

    m_device->m_ctx.api.wgpuQueueSubmit(m_queue, buffers.size(), buffers.data());

    // TODO signal fence
    // m_device->m_ctx.api.wgpuQueueOnSubmittedWorkDone

    return SLANG_OK;
}

Result DeviceImpl::getQueue(QueueType type, ICommandQueue** outQueue)
{
    if (type != QueueType::Graphics)
        return SLANG_FAIL;
    m_queue->establishStrongReferenceToDevice();
    returnComPtr(outQueue, m_queue);
    return SLANG_OK;
}

// CommandEncoderImpl

CommandEncoderImpl::CommandEncoderImpl(DeviceImpl* device, CommandQueueImpl* queue)
    : m_device(device)
    , m_queue(queue)
{
    m_commandBuffer = new CommandBufferImpl(device, queue);
    m_commandBuffer->m_commandList = new CommandList();
    m_commandList = m_commandBuffer->m_commandList;
}

CommandEncoderImpl::~CommandEncoderImpl() {}

Result CommandEncoderImpl::finish(ICommandBuffer** outCommandBuffer)
{
    SLANG_RETURN_ON_FAIL(resolvePipelines(m_device));
    CommandRecorder recorder(m_device);
    SLANG_RETURN_ON_FAIL(recorder.record(m_commandBuffer));
    returnComPtr(outCommandBuffer, m_commandBuffer);
    m_commandBuffer = nullptr;
    m_commandList = nullptr;
    return SLANG_OK;
}

Result CommandEncoderImpl::getNativeHandle(NativeHandle* outHandle)
{
    *outHandle = {};
    return SLANG_E_NOT_AVAILABLE;
}

// CommandBufferImpl

CommandBufferImpl::CommandBufferImpl(DeviceImpl* device, CommandQueueImpl* queue)
    : m_device(device)
    , m_queue(queue)
{
}

CommandBufferImpl::~CommandBufferImpl()
{
    if (m_commandBuffer)
    {
        m_device->m_ctx.api.wgpuCommandBufferRelease(m_commandBuffer);
    }
}

Result CommandBufferImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::WGPUCommandBuffer;
    outHandle->value = (uint64_t)m_commandBuffer;
    return SLANG_OK;
}

} // namespace rhi::wgpu
