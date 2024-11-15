#include "d3d11-command.h"
#include "d3d11-query.h"
#include "d3d11-shader-program.h"
#include "d3d11-input-layout.h"
#include "../command-list.h"
#include "../strings.h"

namespace rhi::d3d11 {

template<typename T>
inline bool arraysEqual(GfxCount countA, GfxCount countB, const T* a, const T* b)
{
    return (countA == countB) ? std::memcmp(a, b, countA * sizeof(T)) == 0 : false;
}

class CommandExecutor
{
public:
    DeviceImpl* m_device;
    ID3D11DeviceContext* m_immediateContext;

    short_vector<RefPtr<TextureViewImpl>> m_renderTargetViews;
    short_vector<RefPtr<TextureViewImpl>> m_resolveTargetViews;
    RefPtr<TextureViewImpl> m_depthStencilView;

    bool m_renderPassActive = false;
    bool m_renderStateValid = false;
    RenderState m_renderState = {};
    RefPtr<RenderPipelineImpl> m_renderPipeline;

    bool m_computePassActive = false;
    bool m_computeStateValid = false;
    RefPtr<ComputePipelineImpl> m_computePipeline;

    RefPtr<BindingDataImpl> m_bindingData;

    bool m_usedDisjointQuery = false;

    CommandExecutor(DeviceImpl* device)
        : m_device(device)
        , m_immediateContext(device->m_immediateContext.get())
    {
    }

    Result execute(CommandBufferImpl* commandBuffer);

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
    void cmdPushDebugGroup(const commands::PushDebugGroup& cmd);
    void cmdPopDebugGroup(const commands::PopDebugGroup& cmd);
    void cmdInsertDebugMarker(const commands::InsertDebugMarker& cmd);
    void cmdWriteTimestamp(const commands::WriteTimestamp& cmd);
    void cmdExecuteCallback(const commands::ExecuteCallback& cmd);

    void clearState();
};

Result CommandExecutor::execute(CommandBufferImpl* commandBuffer)
{
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

#undef NOT_IMPLEMENTED

    if (m_usedDisjointQuery)
    {
        m_immediateContext->End(m_device->m_disjointQuery);
    }

    return SLANG_OK;
}

#define NOT_SUPPORTED(x) m_device->warning(x " command is not supported!")

void CommandExecutor::cmdCopyBuffer(const commands::CopyBuffer& cmd)
{
    BufferImpl* dst = checked_cast<BufferImpl*>(cmd.dst);
    BufferImpl* src = checked_cast<BufferImpl*>(cmd.src);
    D3D11_BOX srcBox = {};
    srcBox.left = (UINT)cmd.srcOffset;
    srcBox.right = (UINT)(cmd.srcOffset + cmd.size);
    srcBox.bottom = srcBox.back = 1;
    m_immediateContext->CopySubresourceRegion(dst->m_buffer, 0, (UINT)cmd.dstOffset, 0, 0, src->m_buffer, 0, &srcBox);
}

void CommandExecutor::cmdCopyTexture(const commands::CopyTexture& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(S_CommandEncoder_copyTexture);
}

void CommandExecutor::cmdCopyTextureToBuffer(const commands::CopyTextureToBuffer& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(S_CommandEncoder_copyTextureToBuffer);
}

void CommandExecutor::cmdClearBuffer(const commands::ClearBuffer& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(S_CommandEncoder_clearBuffer);
}

void CommandExecutor::cmdClearTexture(const commands::ClearTexture& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(S_CommandEncoder_clearTexture);
}

void CommandExecutor::cmdUploadTextureData(const commands::UploadTextureData& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(S_CommandEncoder_uploadTextureData);
}

void CommandExecutor::cmdUploadBufferData(const commands::UploadBufferData& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(S_CommandEncoder_uploadBufferData);
}

void CommandExecutor::cmdResolveQuery(const commands::ResolveQuery& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(S_CommandEncoder_resolveQuery);
}

void CommandExecutor::cmdBeginRenderPass(const commands::BeginRenderPass& cmd)
{
    clearState();

    const RenderPassDesc& desc = cmd.desc;

    m_renderTargetViews.resize(desc.colorAttachmentCount);
    m_resolveTargetViews.resize(desc.colorAttachmentCount);
    for (Index i = 0; i < desc.colorAttachmentCount; ++i)
    {
        m_renderTargetViews[i] = checked_cast<TextureViewImpl*>(desc.colorAttachments[i].view);
        m_resolveTargetViews[i] = checked_cast<TextureViewImpl*>(desc.colorAttachments[i].resolveTarget);
    }
    m_depthStencilView =
        desc.depthStencilAttachment ? checked_cast<TextureViewImpl*>(desc.depthStencilAttachment->view) : nullptr;

    // Clear color attachments.
    for (Index i = 0; i < desc.colorAttachmentCount; ++i)
    {
        const auto& attachment = desc.colorAttachments[i];
        if (attachment.loadOp == LoadOp::Clear)
        {
            m_immediateContext->ClearRenderTargetView(
                checked_cast<TextureViewImpl*>(attachment.view)->getRTV(),
                attachment.clearValue
            );
        }
    }
    // Clear depth/stencil attachment.
    if (desc.depthStencilAttachment)
    {
        const auto& attachment = *desc.depthStencilAttachment;
        UINT clearFlags = 0;
        if (attachment.depthLoadOp == LoadOp::Clear)
        {
            clearFlags |= D3D11_CLEAR_DEPTH;
        }
        if (attachment.stencilLoadOp == LoadOp::Clear)
        {
            clearFlags |= D3D11_CLEAR_STENCIL;
        }
        if (clearFlags)
        {
            m_immediateContext->ClearDepthStencilView(
                checked_cast<TextureViewImpl*>(attachment.view)->getDSV(),
                D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL,
                attachment.depthClearValue,
                attachment.stencilClearValue
            );
        }
    }

    // Set render targets.
    short_vector<ID3D11RenderTargetView*, 8> renderTargetViews(desc.colorAttachmentCount, nullptr);
    for (Index i = 0; i < desc.colorAttachmentCount; ++i)
    {
        renderTargetViews[i] = m_renderTargetViews[i]->getRTV();
    }
    ID3D11DepthStencilView* depthStencilView = m_depthStencilView ? m_depthStencilView->getDSV() : nullptr;
    m_immediateContext->OMSetRenderTargets((UINT)renderTargetViews.size(), renderTargetViews.data(), depthStencilView);

    m_renderPassActive = true;
}

void CommandExecutor::cmdEndRenderPass(const commands::EndRenderPass& cmd)
{
    // Resolve render targets.
    for (size_t i = 0; i < m_renderTargetViews.size(); ++i)
    {
        if (m_renderTargetViews[i] && m_resolveTargetViews[i])
        {
            TextureViewImpl* srcView = m_renderTargetViews[i].get();
            TextureViewImpl* dstView = m_resolveTargetViews[i].get();
            DXGI_FORMAT format = D3DUtil::getMapFormat(srcView->m_texture->m_desc.format);
            m_immediateContext->ResolveSubresource(
                dstView->m_texture->m_resource,
                0, // TODO iterate subresources
                srcView->m_texture->m_resource,
                0, // TODO iterate subresources
                format
            );
        }
    }

    m_renderTargetViews.clear();
    m_resolveTargetViews.clear();
    m_depthStencilView = nullptr;

    m_renderPassActive = false;

    clearState();
}

void CommandExecutor::cmdSetRenderState(const commands::SetRenderState& cmd)
{
    if (!m_renderPassActive)
        return;

    const RenderState& state = cmd.state;

    bool updatePipeline = !m_renderStateValid || cmd.pipeline != m_renderPipeline;
    bool updateBindings = updatePipeline || cmd.bindingData != m_bindingData;
    bool updateDepthStencilState = !m_renderStateValid || state.stencilRef != m_renderState.stencilRef;
    bool updateVertexBuffers = !m_renderStateValid || arraysEqual(
                                                          state.vertexBufferCount,
                                                          m_renderState.vertexBufferCount,
                                                          state.vertexBuffers,
                                                          m_renderState.vertexBuffers
                                                      );
    bool updateIndexBuffer = !m_renderStateValid || state.indexFormat != m_renderState.indexFormat ||
                             state.indexBuffer.buffer != m_renderState.indexBuffer.buffer ||
                             state.indexBuffer.offset != m_renderState.indexBuffer.offset;
    bool updateViewports =
        !m_renderStateValid ||
        arraysEqual(state.viewportCount, m_renderState.viewportCount, state.viewports, m_renderState.viewports);
    bool updateScissorRects = !m_renderStateValid || arraysEqual(
                                                         state.scissorRectCount,
                                                         m_renderState.scissorRectCount,
                                                         state.scissorRects,
                                                         m_renderState.scissorRects
                                                     );

    if (updatePipeline)
    {
        m_renderPipeline = checked_cast<RenderPipelineImpl*>(cmd.pipeline);

        m_immediateContext->IASetInputLayout(m_renderPipeline->m_inputLayout->m_layout);
        m_immediateContext->IASetPrimitiveTopology(m_renderPipeline->m_primitiveTopology);
        m_immediateContext->VSSetShader(m_renderPipeline->m_programImpl->m_vertexShader, nullptr, 0);
        m_immediateContext->RSSetState(m_renderPipeline->m_rasterizerState);
        m_immediateContext->PSSetShader(m_renderPipeline->m_programImpl->m_pixelShader, nullptr, 0);
        m_immediateContext->OMSetBlendState(
            m_renderPipeline->m_blendState,
            m_renderPipeline->m_blendColor,
            m_renderPipeline->m_sampleMask
        );
    }

    if (updateBindings)
    {
        m_bindingData = checked_cast<BindingDataImpl*>(cmd.bindingData);

        // Bind constant buffers, shader resource views, and samplers.
        m_immediateContext->VSSetConstantBuffers(0, (UINT)m_bindingData->cbvs.size(), m_bindingData->cbvs.data());
        m_immediateContext->PSSetConstantBuffers(0, (UINT)m_bindingData->cbvs.size(), m_bindingData->cbvs.data());
        m_immediateContext->VSSetShaderResources(0, (UINT)m_bindingData->srvs.size(), m_bindingData->srvs.data());
        m_immediateContext->PSSetShaderResources(0, (UINT)m_bindingData->srvs.size(), m_bindingData->srvs.data());
        m_immediateContext->VSSetSamplers(0, (UINT)m_bindingData->samplers.size(), m_bindingData->samplers.data());
        m_immediateContext->PSSetSamplers(0, (UINT)m_bindingData->samplers.size(), m_bindingData->samplers.data());

        // Bind unordered access views.
        //
        // In D3D11, the RTV and UAV binding slots alias, so that a shader
        // that binds an RTV for `SV_Target0` cannot also bind a UAV for `u0`.
        // The Slang layout algorithm already accounts for this rule, and assigns
        // all UAVs to slots that won't alias the RTVs it knows about.
        // This means the UAV array in the root object will have all UAVs
        // offset by the number of RTVs that are bound.
        //
        UINT rtvCount = (UINT)m_renderTargetViews.size();
        UINT uavCount = (UINT)m_bindingData->uavs.size();
        SLANG_RHI_ASSERT((uavCount == 0) || (uavCount >= rtvCount));
        if (uavCount)
        {
            m_immediateContext->OMSetRenderTargetsAndUnorderedAccessViews(
                D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL,
                nullptr,
                nullptr,
                rtvCount,
                uavCount - rtvCount,
                m_bindingData->uavs.data() + rtvCount,
                nullptr
            );
        }
    }

    if (updateDepthStencilState)
    {
        m_immediateContext->OMSetDepthStencilState(m_renderPipeline->m_depthStencilState, state.stencilRef);
    }

    if (updateVertexBuffers)
    {
        UINT strides[SLANG_COUNT_OF(state.vertexBuffers)];
        UINT offsets[SLANG_COUNT_OF(state.vertexBuffers)];
        ID3D11Buffer* buffers[SLANG_COUNT_OF(state.vertexBuffers)];
        for (Index i = 0; i < state.vertexBufferCount; ++i)
        {
            const auto& buffer = state.vertexBuffers[i];
            strides[i] = m_renderPipeline->m_inputLayout->m_vertexStreamStrides[i];
            offsets[i] = state.vertexBuffers[i].offset;
            buffers[i] = checked_cast<BufferImpl*>(state.vertexBuffers[i].buffer)->m_buffer;
        }
        m_immediateContext->IASetVertexBuffers(0, state.vertexBufferCount, buffers, strides, offsets);
    }

    if (updateIndexBuffer)
    {
        if (state.indexBuffer.buffer)
        {
            m_immediateContext->IASetIndexBuffer(
                checked_cast<BufferImpl*>(state.indexBuffer.buffer)->m_buffer,
                D3DUtil::getIndexFormat(state.indexFormat),
                (UINT)state.indexBuffer.offset
            );
        }
        else
        {
            m_immediateContext->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
        }
    }

    if (updateViewports)
    {
        static const int kMaxViewports = D3D11_VIEWPORT_AND_SCISSORRECT_MAX_INDEX + 1;
        SLANG_RHI_ASSERT(state.viewportCount <= kMaxViewports);
        D3D11_VIEWPORT viewports[SLANG_COUNT_OF(state.viewports)];
        for (GfxIndex i = 0; i < state.viewportCount; ++i)
        {
            const Viewport& src = state.viewports[i];
            D3D11_VIEWPORT& dst = viewports[i];
            dst.TopLeftX = src.originX;
            dst.TopLeftY = src.originY;
            dst.Width = src.extentX;
            dst.Height = src.extentY;
            dst.MinDepth = src.minZ;
            dst.MaxDepth = src.maxZ;
        }
        m_immediateContext->RSSetViewports(UINT(state.viewportCount), viewports);
    }

    if (updateScissorRects)
    {
        static const int kMaxScissorRects = D3D11_VIEWPORT_AND_SCISSORRECT_MAX_INDEX + 1;
        SLANG_RHI_ASSERT(state.scissorRectCount <= kMaxScissorRects);
        D3D11_RECT scissorRects[SLANG_COUNT_OF(state.scissorRects)];
        for (GfxIndex i = 0; i < state.scissorRectCount; ++i)
        {
            const ScissorRect& src = state.scissorRects[i];
            D3D11_RECT& dst = scissorRects[i];
            dst.left = LONG(src.minX);
            dst.top = LONG(src.minY);
            dst.right = LONG(src.maxX);
            dst.bottom = LONG(src.maxY);
        }
        m_immediateContext->RSSetScissorRects(UINT(state.scissorRectCount), scissorRects);
    }

    m_renderStateValid = true;
    m_renderState = state;
}

void CommandExecutor::cmdDraw(const commands::Draw& cmd)
{
    if (!m_renderStateValid)
        return;

    m_immediateContext->DrawInstanced(
        cmd.args.vertexCount,
        cmd.args.instanceCount,
        cmd.args.startVertexLocation,
        cmd.args.startIndexLocation
    );
}

void CommandExecutor::cmdDrawIndexed(const commands::DrawIndexed& cmd)
{
    if (!m_renderStateValid)
        return;

    m_immediateContext->DrawIndexedInstanced(
        cmd.args.vertexCount,
        cmd.args.instanceCount,
        cmd.args.startIndexLocation,
        cmd.args.startVertexLocation,
        cmd.args.startInstanceLocation
    );
}

void CommandExecutor::cmdDrawIndirect(const commands::DrawIndirect& cmd)
{
    if (!m_renderStateValid)
        return;

    // D3D11 does not support sourcing the count from a buffer.
    if (cmd.countBuffer)
    {
        m_device->warning(S_RenderPassEncoder_drawIndirect " with countBuffer not supported");
        return;
    }

    auto argBuffer = checked_cast<BufferImpl*>(cmd.argBuffer);

    m_immediateContext->DrawInstancedIndirect(argBuffer->m_buffer, cmd.argOffset);
}

void CommandExecutor::cmdDrawIndexedIndirect(const commands::DrawIndexedIndirect& cmd)
{
    if (!m_renderStateValid)
        return;

    // D3D11 does not support sourcing the count from a buffer.
    if (cmd.countBuffer)
    {
        m_device->warning(S_RenderPassEncoder_drawIndirect " with countBuffer not supported");
        return;
    }

    auto argBuffer = checked_cast<BufferImpl*>(cmd.argBuffer);

    m_immediateContext->DrawIndexedInstancedIndirect(argBuffer->m_buffer, cmd.argOffset);
}

void CommandExecutor::cmdDrawMeshTasks(const commands::DrawMeshTasks& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(S_RenderPassEncoder_drawMeshTasks);
}

void CommandExecutor::cmdBeginComputePass(const commands::BeginComputePass& cmd)
{
    SLANG_UNUSED(cmd);
    m_computePassActive = true;
}

void CommandExecutor::cmdEndComputePass(const commands::EndComputePass& cmd)
{
    SLANG_UNUSED(cmd);
    m_computePassActive = false;
    clearState();
}

void CommandExecutor::cmdSetComputeState(const commands::SetComputeState& cmd)
{
    if (!m_computePassActive)
        return;

    bool updatePipeline = !m_computeStateValid || cmd.pipeline != m_computePipeline;
    bool updateBindings = updatePipeline || cmd.bindingData != m_bindingData;

    if (updatePipeline)
    {
        m_computePipeline = checked_cast<ComputePipelineImpl*>(cmd.pipeline);
        m_immediateContext->CSSetShader(m_computePipeline->m_programImpl->m_computeShader, nullptr, 0);
    }

    if (updateBindings)
    {
        m_bindingData = checked_cast<BindingDataImpl*>(cmd.bindingData);

        m_immediateContext->CSSetConstantBuffers(0, (UINT)m_bindingData->cbvs.size(), m_bindingData->cbvs.data());
        m_immediateContext->CSSetShaderResources(0, (UINT)m_bindingData->srvs.size(), m_bindingData->srvs.data());
        m_immediateContext->CSSetSamplers(0, (UINT)m_bindingData->samplers.size(), m_bindingData->samplers.data());
        m_immediateContext
            ->CSSetUnorderedAccessViews(0, (UINT)m_bindingData->uavs.size(), m_bindingData->uavs.data(), nullptr);
    }

    m_computeStateValid = true;
}

void CommandExecutor::cmdDispatchCompute(const commands::DispatchCompute& cmd)
{
    if (!m_computeStateValid)
        return;

    m_immediateContext->Dispatch(cmd.x, cmd.y, cmd.z);
}

void CommandExecutor::cmdDispatchComputeIndirect(const commands::DispatchComputeIndirect& cmd)
{
    BufferImpl* argBuffer = checked_cast<BufferImpl*>(cmd.argBuffer);
    m_immediateContext->DispatchIndirect(argBuffer->m_buffer, cmd.offset);
}

void CommandExecutor::cmdBeginRayTracingPass(const commands::BeginRayTracingPass& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(S_CommandEncoder_beginRayTracingPass);
}

void CommandExecutor::cmdEndRayTracingPass(const commands::EndRayTracingPass& cmd)
{
    SLANG_UNUSED(cmd);
}

void CommandExecutor::cmdSetRayTracingState(const commands::SetRayTracingState& cmd)
{
    SLANG_UNUSED(cmd);
}

void CommandExecutor::cmdDispatchRays(const commands::DispatchRays& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(S_RayTracingPassEncoder_dispatchRays);
}

void CommandExecutor::cmdBuildAccelerationStructure(const commands::BuildAccelerationStructure& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(S_CommandEncoder_buildAccelerationStructure);
}

void CommandExecutor::cmdCopyAccelerationStructure(const commands::CopyAccelerationStructure& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(S_CommandEncoder_copyAccelerationStructure);
}

void CommandExecutor::cmdQueryAccelerationStructureProperties(const commands::QueryAccelerationStructureProperties& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(S_CommandEncoder_queryAccelerationStructureProperties);
}

void CommandExecutor::cmdSerializeAccelerationStructure(const commands::SerializeAccelerationStructure& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(S_CommandEncoder_serializeAccelerationStructure);
}

void CommandExecutor::cmdDeserializeAccelerationStructure(const commands::DeserializeAccelerationStructure& cmd)
{
    SLANG_UNUSED(cmd);
    NOT_SUPPORTED(S_CommandEncoder_deserializeAccelerationStructure);
}

void CommandExecutor::cmdSetBufferState(const commands::SetBufferState& cmd)
{
    SLANG_UNUSED(cmd);
}

void CommandExecutor::cmdSetTextureState(const commands::SetTextureState& cmd)
{
    SLANG_UNUSED(cmd);
}

void CommandExecutor::cmdPushDebugGroup(const commands::PushDebugGroup& cmd)
{
    SLANG_UNUSED(cmd);
}

void CommandExecutor::cmdPopDebugGroup(const commands::PopDebugGroup& cmd)
{
    SLANG_UNUSED(cmd);
}

void CommandExecutor::cmdInsertDebugMarker(const commands::InsertDebugMarker& cmd)
{
    SLANG_UNUSED(cmd);
}

void CommandExecutor::cmdWriteTimestamp(const commands::WriteTimestamp& cmd)
{
    auto queryPool = checked_cast<QueryPoolImpl*>(cmd.queryPool);
    if (!m_usedDisjointQuery)
    {
        m_immediateContext->Begin(m_device->m_disjointQuery);
        m_usedDisjointQuery = true;
    }
    m_immediateContext->End(queryPool->getQuery(cmd.queryIndex));
}

void CommandExecutor::cmdExecuteCallback(const commands::ExecuteCallback& cmd)
{
    cmd.callback(cmd.userData);
}

void CommandExecutor::clearState()
{
    m_immediateContext->ClearState();
    m_renderStateValid = false;
    m_renderState = {};
    m_renderPipeline = nullptr;
    m_computeStateValid = false;
    m_computePipeline = nullptr;
    m_bindingData = nullptr;
}

// CommandQueueImpl

CommandQueueImpl::CommandQueueImpl(DeviceImpl* device, QueueType type)
    : CommandQueue(device, type)
{
}

Result CommandQueueImpl::createCommandEncoder(ICommandEncoder** outEncoder)
{
    RefPtr<CommandEncoderImpl> encoder = new CommandEncoderImpl(m_device);
    SLANG_RETURN_ON_FAIL(encoder->init());
    returnComPtr(outEncoder, encoder);
    return SLANG_OK;
}

Result CommandQueueImpl::submit(
    GfxCount count,
    ICommandBuffer* const* commandBuffers,
    IFence* fenceToSignal,
    uint64_t newFenceValue
)
{
    for (GfxIndex i = 0; i < count; i++)
    {
        CommandExecutor executor(m_device);
        SLANG_RETURN_ON_FAIL(executor.execute(checked_cast<CommandBufferImpl*>(commandBuffers[i])));
    }
    return SLANG_OK;
}

Result CommandQueueImpl::getNativeHandle(NativeHandle* outHandle)
{
    *outHandle = {};
    return SLANG_E_NOT_AVAILABLE;
}

Result CommandQueueImpl::waitOnHost()
{
    return SLANG_OK;
}

Result CommandQueueImpl::waitForFenceValuesOnDevice(GfxCount fenceCount, IFence** fences, uint64_t* waitValues)
{
    return SLANG_E_NOT_AVAILABLE;
}

// CommandEncoderImpl

CommandEncoderImpl::CommandEncoderImpl(DeviceImpl* device)
    : m_device(device)
{
}

Result CommandEncoderImpl::init()
{
    m_commandBuffer = new CommandBufferImpl();
    m_commandBuffer->m_commandList = new CommandList();
    m_commandBuffer->m_bindingCache = new BindingCache();
    m_commandList = m_commandBuffer->m_commandList;
    return SLANG_OK;
}

Result CommandEncoderImpl::createRootShaderObject(IShaderProgram* program, IShaderObject** outObject)
{
    return m_device->createRootShaderObject(program, outObject);
}

Result CommandEncoderImpl::getBindingData(IShaderObject* object, BindingData*& outBindingData)
{
    RootShaderObjectImpl* rootObject = checked_cast<RootShaderObjectImpl*>(object);
    RefPtr<ShaderObjectLayoutImpl> specializedLayout;
    SLANG_RETURN_ON_FAIL(rootObject->_getSpecializedLayout(specializedLayout.writeRef()));
    RootShaderObjectLayoutImpl* specializedRootLayout =
        checked_cast<RootShaderObjectLayoutImpl*>(specializedLayout.get());
    BindingContext bindingContext(m_device, m_commandBuffer->m_bindingCache);
    SLANG_RETURN_ON_FAIL(
        rootObject->bindAsRoot(bindingContext, specializedRootLayout, (BindingDataImpl*&)outBindingData)
    );
    return SLANG_OK;
}

Result CommandEncoderImpl::finish(ICommandBuffer** outCommandBuffer)
{
    SLANG_RETURN_ON_FAIL(resolvePipelines(m_device));
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

Result CommandBufferImpl::getNativeHandle(NativeHandle* outHandle)
{
    *outHandle = {};
    return SLANG_E_NOT_AVAILABLE;
}

} // namespace rhi::d3d11
