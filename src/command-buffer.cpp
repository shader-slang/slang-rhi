#include "command-buffer.h"

#include "rhi-shared.h"
#include "device.h"

namespace rhi {

// ----------------------------------------------------------------------------
// RenderPassEncoder
// ----------------------------------------------------------------------------

IRenderPassEncoder* RenderPassEncoder::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IRenderPassEncoder::getTypeGuid())
        return static_cast<IRenderPassEncoder*>(this);
    return nullptr;
}

RenderPassEncoder::RenderPassEncoder(CommandEncoder* commandEncoder)
    : m_commandEncoder(commandEncoder)
{
}

void RenderPassEncoder::writeRenderState()
{
    commands::SetRenderState cmd;
    cmd.state = m_renderState;
    cmd.pipeline = m_pipeline;
    // TODO(shaderobject) handle errors
    m_commandEncoder->getPipelineSpecializationArgs(m_pipeline, m_rootObject, cmd.specializationArgs);
    m_commandEncoder->getBindingData(m_rootObject, cmd.bindingData);
    m_commandList->write(std::move(cmd));
}

IShaderObject* RenderPassEncoder::bindPipeline(IRenderPipeline* pipeline)
{
    if (m_commandList)
    {
        m_pipeline = pipeline;
        ShaderProgram* program = checked_cast<ShaderProgram*>(pipeline->getProgram());
        if (!SLANG_SUCCEEDED(m_commandEncoder->getDevice()->createRootShaderObject(program, m_rootObject.writeRef())))
            return nullptr;
        return m_rootObject;
    }
    return nullptr;
}

void RenderPassEncoder::bindPipeline(IRenderPipeline* pipeline, IShaderObject* rootObject)
{
    if (m_commandList)
    {
        m_pipeline = checked_cast<RenderPipeline*>(pipeline);
        m_rootObject = checked_cast<RootShaderObject*>(rootObject);
    }
}

void RenderPassEncoder::setRenderState(const RenderState& state)
{
    if (m_commandList)
    {
        m_renderState = state;
    }
}

void RenderPassEncoder::draw(const DrawArguments& args)
{
    if (m_commandList)
    {
        writeRenderState();
        commands::Draw cmd;
        cmd.args = args;
        m_commandList->write(std::move(cmd));
    }
}

void RenderPassEncoder::drawIndexed(const DrawArguments& args)
{
    if (m_commandList)
    {
        writeRenderState();
        commands::DrawIndexed cmd;
        cmd.args = args;
        m_commandList->write(std::move(cmd));
    }
}

void RenderPassEncoder::drawIndirect(uint32_t maxDrawCount, BufferOffsetPair argBuffer, BufferOffsetPair countBuffer)
{
    if (m_commandList)
    {
        writeRenderState();
        commands::DrawIndirect cmd;
        cmd.maxDrawCount = maxDrawCount;
        cmd.argBuffer = argBuffer;
        cmd.countBuffer = countBuffer;
        m_commandList->write(std::move(cmd));
    }
}

void RenderPassEncoder::drawIndexedIndirect(
    uint32_t maxDrawCount,
    BufferOffsetPair argBuffer,
    BufferOffsetPair countBuffer
)
{
    if (m_commandList)
    {
        writeRenderState();
        commands::DrawIndexedIndirect cmd;
        cmd.maxDrawCount = maxDrawCount;
        cmd.argBuffer = argBuffer;
        cmd.countBuffer = countBuffer;
        m_commandList->write(std::move(cmd));
    }
}

void RenderPassEncoder::drawMeshTasks(uint32_t x, uint32_t y, uint32_t z)
{
    if (m_commandList)
    {
        writeRenderState();
        commands::DrawMeshTasks cmd;
        cmd.x = x;
        cmd.y = y;
        cmd.z = z;
        m_commandList->write(std::move(cmd));
    }
}

void RenderPassEncoder::pushDebugGroup(const char* name, float rgbColor[3])
{
    if (m_commandList)
    {
        commands::PushDebugGroup cmd;
        cmd.name = name;
        cmd.rgbColor[0] = rgbColor[0];
        cmd.rgbColor[1] = rgbColor[1];
        cmd.rgbColor[2] = rgbColor[2];
        m_commandList->write(std::move(cmd));
    }
}

void RenderPassEncoder::popDebugGroup()
{
    if (m_commandList)
    {
        commands::PopDebugGroup cmd;
        m_commandList->write(std::move(cmd));
    }
}

void RenderPassEncoder::insertDebugMarker(const char* name, float rgbColor[3])
{
    if (m_commandList)
    {
        commands::InsertDebugMarker cmd;
        cmd.name = name;
        cmd.rgbColor[0] = rgbColor[0];
        cmd.rgbColor[1] = rgbColor[1];
        cmd.rgbColor[2] = rgbColor[2];
        m_commandList->write(std::move(cmd));
    }
}

void RenderPassEncoder::end()
{
    if (m_commandList)
    {
        commands::EndRenderPass cmd;
        m_commandList->write(std::move(cmd));
        m_commandList = nullptr;
    }
}

// ----------------------------------------------------------------------------
// ComputePassEncoder
// ----------------------------------------------------------------------------

IComputePassEncoder* ComputePassEncoder::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IComputePassEncoder::getTypeGuid())
        return static_cast<IComputePassEncoder*>(this);
    return nullptr;
}

ComputePassEncoder::ComputePassEncoder(CommandEncoder* commandEncoder)
    : m_commandEncoder(commandEncoder)
{
}

void ComputePassEncoder::writeComputeState()
{
    commands::SetComputeState cmd;
    cmd.pipeline = m_pipeline;
    // TODO(shaderobject) handle errors
    m_commandEncoder->getPipelineSpecializationArgs(m_pipeline, m_rootObject, cmd.specializationArgs);
    m_commandEncoder->getBindingData(m_rootObject, cmd.bindingData);
    m_commandList->write(std::move(cmd));
}

IShaderObject* ComputePassEncoder::bindPipeline(IComputePipeline* pipeline)
{
    if (m_commandList)
    {
        m_pipeline = pipeline;
        ShaderProgram* program = checked_cast<ShaderProgram*>(pipeline->getProgram());
        if (!SLANG_SUCCEEDED(m_commandEncoder->getDevice()->createRootShaderObject(program, m_rootObject.writeRef())))
            return nullptr;
        return m_rootObject;
    }
    return nullptr;
}

void ComputePassEncoder::bindPipeline(IComputePipeline* pipeline, IShaderObject* rootObject)
{
    if (m_commandList)
    {
        m_pipeline = checked_cast<ComputePipeline*>(pipeline);
        m_rootObject = checked_cast<RootShaderObject*>(rootObject);
    }
}

void ComputePassEncoder::dispatchCompute(uint32_t x, uint32_t y, uint32_t z)
{
    if (m_commandList)
    {
        writeComputeState();
        commands::DispatchCompute cmd;
        cmd.x = x;
        cmd.y = y;
        cmd.z = z;
        m_commandList->write(std::move(cmd));
    }
}

void ComputePassEncoder::dispatchComputeIndirect(BufferOffsetPair argBuffer)
{
    if (m_commandList)
    {
        writeComputeState();
        commands::DispatchComputeIndirect cmd;
        cmd.argBuffer = argBuffer;
        m_commandList->write(std::move(cmd));
    }
}

void ComputePassEncoder::pushDebugGroup(const char* name, float rgbColor[3])
{
    if (m_commandList)
    {
        commands::PushDebugGroup cmd;
        cmd.name = name;
        cmd.rgbColor[0] = rgbColor[0];
        cmd.rgbColor[1] = rgbColor[1];
        cmd.rgbColor[2] = rgbColor[2];
        m_commandList->write(std::move(cmd));
    }
}

void ComputePassEncoder::popDebugGroup()
{
    if (m_commandList)
    {
        commands::PopDebugGroup cmd;
        m_commandList->write(std::move(cmd));
    }
}

void ComputePassEncoder::insertDebugMarker(const char* name, float rgbColor[3])
{
    if (m_commandList)
    {
        commands::InsertDebugMarker cmd;
        cmd.name = name;
        cmd.rgbColor[0] = rgbColor[0];
        cmd.rgbColor[1] = rgbColor[1];
        cmd.rgbColor[2] = rgbColor[2];
        m_commandList->write(std::move(cmd));
    }
}

void ComputePassEncoder::end()
{
    if (m_commandList)
    {
        commands::EndComputePass cmd;
        m_commandList->write(std::move(cmd));
        m_commandList = nullptr;
    }
}

// ----------------------------------------------------------------------------
// RayTracingPassEncoder
// ----------------------------------------------------------------------------

IRayTracingPassEncoder* RayTracingPassEncoder::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == IRayTracingPassEncoder::getTypeGuid())
        return static_cast<IRayTracingPassEncoder*>(this);
    return nullptr;
}

RayTracingPassEncoder::RayTracingPassEncoder(CommandEncoder* commandEncoder)
    : m_commandEncoder(commandEncoder)
{
}

void RayTracingPassEncoder::writeRayTracingState()
{
    commands::SetRayTracingState cmd;
    cmd.pipeline = m_pipeline;
    cmd.shaderTable = m_shaderTable;
    // TODO handle errors
    m_commandEncoder->getPipelineSpecializationArgs(m_pipeline, m_rootObject, cmd.specializationArgs);
    m_commandEncoder->getBindingData(m_rootObject, cmd.bindingData);
    m_commandList->write(std::move(cmd));
}

IShaderObject* RayTracingPassEncoder::bindPipeline(IRayTracingPipeline* pipeline, IShaderTable* shaderTable)
{
    if (m_commandList)
    {
        m_pipeline = pipeline;
        m_shaderTable = shaderTable;
        ShaderProgram* program = checked_cast<ShaderProgram*>(pipeline->getProgram());
        if (!SLANG_SUCCEEDED(m_commandEncoder->getDevice()->createRootShaderObject(program, m_rootObject.writeRef())))
            return nullptr;
        return m_rootObject;
    }
    return nullptr;
}

void RayTracingPassEncoder::bindPipeline(
    IRayTracingPipeline* pipeline,
    IShaderTable* shaderTable,
    IShaderObject* rootObject
)
{
    if (m_commandList)
    {
        m_pipeline = checked_cast<RayTracingPipeline*>(pipeline);
        m_shaderTable = checked_cast<ShaderTable*>(shaderTable);
        m_rootObject = checked_cast<RootShaderObject*>(rootObject);
    }
}

void RayTracingPassEncoder::dispatchRays(uint32_t rayGenShaderIndex, uint32_t width, uint32_t height, uint32_t depth)
{
    if (m_commandList)
    {
        writeRayTracingState();
        commands::DispatchRays cmd;
        cmd.rayGenShaderIndex = rayGenShaderIndex;
        cmd.width = width;
        cmd.height = height;
        cmd.depth = depth;
        m_commandList->write(std::move(cmd));
    }
}

void RayTracingPassEncoder::pushDebugGroup(const char* name, float rgbColor[3])
{
    if (m_commandList)
    {
        commands::PushDebugGroup cmd;
        cmd.name = name;
        cmd.rgbColor[0] = rgbColor[0];
        cmd.rgbColor[1] = rgbColor[1];
        cmd.rgbColor[2] = rgbColor[2];
        m_commandList->write(std::move(cmd));
    }
}

void RayTracingPassEncoder::popDebugGroup()
{
    if (m_commandList)
    {
        commands::PopDebugGroup cmd;
        m_commandList->write(std::move(cmd));
    }
}

void RayTracingPassEncoder::insertDebugMarker(const char* name, float rgbColor[3])
{
    if (m_commandList)
    {
        commands::InsertDebugMarker cmd;
        cmd.name = name;
        cmd.rgbColor[0] = rgbColor[0];
        cmd.rgbColor[1] = rgbColor[1];
        cmd.rgbColor[2] = rgbColor[2];
        m_commandList->write(std::move(cmd));
    }
}

void RayTracingPassEncoder::end()
{
    if (m_commandList)
    {
        commands::EndRayTracingPass cmd;
        m_commandList->write(std::move(cmd));
        m_commandList = nullptr;
    }
}

// ----------------------------------------------------------------------------
// CommandEncoder
// ----------------------------------------------------------------------------

ICommandEncoder* CommandEncoder::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == ICommandEncoder::getTypeGuid())
        return static_cast<ICommandEncoder*>(this);
    return nullptr;
}

CommandEncoder::CommandEncoder()
    : m_renderPassEncoder(this)
    , m_computePassEncoder(this)
    , m_rayTracingPassEncoder(this)
{
}

IRenderPassEncoder* CommandEncoder::beginRenderPass(const RenderPassDesc& desc)
{
    commands::BeginRenderPass cmd;
    cmd.desc = desc;
    m_commandList->write(std::move(cmd));
    m_renderPassEncoder.m_commandList = m_commandList;
    return &m_renderPassEncoder;
}

IComputePassEncoder* CommandEncoder::beginComputePass()
{
    commands::BeginComputePass cmd;
    m_commandList->write(std::move(cmd));
    m_computePassEncoder.m_commandList = m_commandList;
    return &m_computePassEncoder;
}

IRayTracingPassEncoder* CommandEncoder::beginRayTracingPass()
{
    commands::BeginRayTracingPass cmd;
    m_commandList->write(std::move(cmd));
    m_rayTracingPassEncoder.m_commandList = m_commandList;
    return &m_rayTracingPassEncoder;
}

void CommandEncoder::copyBuffer(IBuffer* dst, Offset dstOffset, IBuffer* src, Offset srcOffset, Size size)
{
    commands::CopyBuffer cmd;
    cmd.dst = dst;
    cmd.dstOffset = dstOffset;
    cmd.src = src;
    cmd.srcOffset = srcOffset;
    cmd.size = size;
    m_commandList->write(std::move(cmd));
}

void CommandEncoder::copyTexture(
    ITexture* dst,
    SubresourceRange dstSubresource,
    Offset3D dstOffset,
    ITexture* src,
    SubresourceRange srcSubresource,
    Offset3D srcOffset,
    Extents extent
)
{
    commands::CopyTexture cmd;
    cmd.dst = dst;
    cmd.dstSubresource = dstSubresource;
    cmd.dstOffset = dstOffset;
    cmd.src = src;
    cmd.srcSubresource = srcSubresource;
    cmd.srcOffset = srcOffset;
    cmd.extent = extent;
    m_commandList->write(std::move(cmd));
}

void CommandEncoder::copyTextureToBuffer(
    IBuffer* dst,
    Offset dstOffset,
    Size dstSize,
    Size dstRowStride,
    ITexture* src,
    SubresourceRange srcSubresource,
    Offset3D srcOffset,
    Extents extent
)
{
    commands::CopyTextureToBuffer cmd;
    cmd.dst = dst;
    cmd.dstOffset = dstOffset;
    cmd.dstSize = dstSize;
    cmd.dstRowStride = dstRowStride;
    cmd.src = src;
    cmd.srcSubresource = srcSubresource;
    cmd.srcOffset = srcOffset;
    cmd.extent = extent;
    m_commandList->write(std::move(cmd));
}

void CommandEncoder::uploadTextureData(
    ITexture* dst,
    SubresourceRange subresourceRange,
    Offset3D offset,
    Extents extent,
    SubresourceData* subresourceData,
    uint32_t subresourceDataCount
)
{
    commands::UploadTextureData cmd;
    cmd.dst = dst;
    cmd.subresourceRange = subresourceRange;
    cmd.offset = offset;
    cmd.extent = extent;
    cmd.subresourceData = subresourceData;
    cmd.subresourceDataCount = subresourceDataCount;
    m_commandList->write(std::move(cmd));
}

void CommandEncoder::uploadBufferData(IBuffer* dst, Offset offset, Size size, void* data)
{
    RefPtr<StagingHeap::Handle> handle = getDevice()->m_heap.allocHandle(size, {});

    m_commandList->retainResource(handle);

    commands::CopyBuffer cmd;

    cmd.dst = dst;
    cmd.dstOffset = offset;
    cmd.src = handle->getBuffer();
    cmd.srcOffset = handle->getOffset();
    cmd.size = size;

    void* buffer;
    getDevice()->mapBuffer(cmd.src, CpuAccessMode::Write, &buffer);
    memcpy(((uint8_t*)buffer) + cmd.srcOffset, data, cmd.size);
    getDevice()->unmapBuffer(cmd.src);

    m_commandList->write(std::move(cmd));
}

void CommandEncoder::clearBuffer(IBuffer* buffer, const BufferRange* range)
{
    commands::ClearBuffer cmd;
    cmd.buffer = buffer;
    cmd.range = range ? *range : checked_cast<Buffer*>(buffer)->resolveBufferRange(kEntireBuffer);
    m_commandList->write(std::move(cmd));
}

void CommandEncoder::clearTexture(
    ITexture* texture,
    const ClearValue& clearValue,
    const SubresourceRange* subresourceRange,
    bool clearDepth,
    bool clearStencil
)
{
    commands::ClearTexture cmd;
    cmd.texture = texture;
    cmd.clearValue = clearValue;
    cmd.subresourceRange =
        subresourceRange ? *subresourceRange : checked_cast<Texture*>(texture)->resolveSubresourceRange(kEntireTexture);
    cmd.clearDepth = clearDepth;
    cmd.clearStencil = clearStencil;
    m_commandList->write(std::move(cmd));
}

void CommandEncoder::resolveQuery(
    IQueryPool* queryPool,
    uint32_t index,
    uint32_t count,
    IBuffer* buffer,
    uint64_t offset
)
{
    commands::ResolveQuery cmd;
    cmd.queryPool = queryPool;
    cmd.index = index;
    cmd.count = count;
    cmd.buffer = buffer;
    cmd.offset = offset;
    m_commandList->write(std::move(cmd));
}

void CommandEncoder::buildAccelerationStructure(
    const AccelerationStructureBuildDesc& desc,
    IAccelerationStructure* dst,
    IAccelerationStructure* src,
    BufferOffsetPair scratchBuffer,
    uint32_t propertyQueryCount,
    AccelerationStructureQueryDesc* queryDescs
)
{
    commands::BuildAccelerationStructure cmd;
    cmd.desc = desc;
    cmd.dst = dst;
    cmd.src = src;
    cmd.scratchBuffer = scratchBuffer;
    cmd.propertyQueryCount = propertyQueryCount;
    cmd.queryDescs = queryDescs;
    m_commandList->write(std::move(cmd));
}

void CommandEncoder::copyAccelerationStructure(
    IAccelerationStructure* dst,
    IAccelerationStructure* src,
    AccelerationStructureCopyMode mode
)
{
    commands::CopyAccelerationStructure cmd;
    cmd.dst = checked_cast<AccelerationStructure*>(dst);
    cmd.src = checked_cast<AccelerationStructure*>(src);
    cmd.mode = mode;
    m_commandList->write(std::move(cmd));
}

void CommandEncoder::queryAccelerationStructureProperties(
    uint32_t accelerationStructureCount,
    IAccelerationStructure** accelerationStructures,
    uint32_t queryCount,
    AccelerationStructureQueryDesc* queryDescs
)
{
    SLANG_RHI_UNIMPLEMENTED("queryAccelerationStructureProperties");
}

void CommandEncoder::serializeAccelerationStructure(BufferOffsetPair dst, IAccelerationStructure* src)
{
    commands::SerializeAccelerationStructure cmd;
    cmd.dst = dst;
    cmd.src = checked_cast<AccelerationStructure*>(src);
    m_commandList->write(std::move(cmd));
}

void CommandEncoder::deserializeAccelerationStructure(IAccelerationStructure* dst, BufferOffsetPair src)
{
    commands::DeserializeAccelerationStructure cmd;
    cmd.dst = checked_cast<AccelerationStructure*>(dst);
    cmd.src = src;
    m_commandList->write(std::move(cmd));
}

void CommandEncoder::convertCooperativeVectorMatrix(const ConvertCooperativeVectorMatrixDesc* descs, uint32_t descCount)
{
    commands::ConvertCooperativeVectorMatrix cmd;
    cmd.descs = descs;
    cmd.descCount = descCount;
    m_commandList->write(std::move(cmd));
}

void CommandEncoder::setBufferState(IBuffer* buffer, ResourceState state)
{
    commands::SetBufferState cmd;
    cmd.buffer = checked_cast<Buffer*>(buffer);
    cmd.state = state;
    m_commandList->write(std::move(cmd));
}

void CommandEncoder::setTextureState(ITexture* texture, SubresourceRange subresourceRange, ResourceState state)
{
    commands::SetTextureState cmd;
    cmd.texture = checked_cast<Texture*>(texture);
    cmd.subresourceRange = subresourceRange;
    cmd.state = state;
    m_commandList->write(std::move(cmd));
}

void CommandEncoder::pushDebugGroup(const char* name, float rgbColor[3])
{
    commands::PushDebugGroup cmd;
    cmd.name = name;
    cmd.rgbColor[0] = rgbColor[0];
    cmd.rgbColor[1] = rgbColor[1];
    cmd.rgbColor[2] = rgbColor[2];
    m_commandList->write(std::move(cmd));
}

void CommandEncoder::popDebugGroup()
{
    commands::PopDebugGroup cmd;
    m_commandList->write(std::move(cmd));
}

void CommandEncoder::insertDebugMarker(const char* name, float rgbColor[3])
{
    commands::InsertDebugMarker cmd;
    cmd.name = name;
    cmd.rgbColor[0] = rgbColor[0];
    cmd.rgbColor[1] = rgbColor[1];
    cmd.rgbColor[2] = rgbColor[2];
    m_commandList->write(std::move(cmd));
}

void CommandEncoder::writeTimestamp(IQueryPool* queryPool, uint32_t queryIndex)
{
    commands::WriteTimestamp cmd;
    cmd.queryPool = checked_cast<QueryPool*>(queryPool);
    cmd.queryIndex = queryIndex;
    m_commandList->write(std::move(cmd));
}

Result CommandEncoder::finish(ICommandBuffer** outCommandBuffer)
{
    // iterate over commands and specialize pipelines
    return SLANG_FAIL;
}

Result CommandEncoder::getPipelineSpecializationArgs(
    IPipeline* pipeline,
    IShaderObject* object,
    ExtendedShaderObjectTypeListObject*& outSpecializationArgs
)
{
    if (checked_cast<ShaderProgram*>(pipeline->getProgram())->isSpecializable())
    {
        RootShaderObject* rootObject = checked_cast<RootShaderObject*>(object);
        RefPtr<ExtendedShaderObjectTypeListObject> specializationArgs = new ExtendedShaderObjectTypeListObject();
        rootObject->collectSpecializationArgs(*specializationArgs);
        m_pipelineSpecializationArgs.push_back(specializationArgs);
        outSpecializationArgs = specializationArgs.get();
    }
    else
    {
        outSpecializationArgs = nullptr;
    }
    return SLANG_OK;
}

Result CommandEncoder::resolvePipelines(Device* device)
{
    CommandList* commandList = m_commandList;
    auto command = commandList->getCommands();
    while (command)
    {
        if (command->id == CommandID::SetRenderState)
        {
            auto& cmd = commandList->getCommand<commands::SetRenderState>(command);
            RenderPipeline* pipeline = checked_cast<RenderPipeline*>(cmd.pipeline);
            auto specializationArgs = static_cast<ExtendedShaderObjectTypeListObject*>(cmd.specializationArgs);
            Pipeline* concretePipeline = nullptr;
            SLANG_RETURN_ON_FAIL(device->getConcretePipeline(pipeline, specializationArgs, concretePipeline));
            cmd.pipeline = static_cast<RenderPipeline*>(concretePipeline);
            cmd.specializationArgs = nullptr;
        }
        else if (command->id == CommandID::SetComputeState)
        {
            auto& cmd = commandList->getCommand<commands::SetComputeState>(command);
            ComputePipeline* pipeline = checked_cast<ComputePipeline*>(cmd.pipeline);
            auto specializationArgs = static_cast<ExtendedShaderObjectTypeListObject*>(cmd.specializationArgs);
            Pipeline* concretePipeline = nullptr;
            SLANG_RETURN_ON_FAIL(device->getConcretePipeline(pipeline, specializationArgs, concretePipeline));
            cmd.pipeline = static_cast<ComputePipeline*>(concretePipeline);
            cmd.specializationArgs = nullptr;
        }
        else if (command->id == CommandID::SetRayTracingState)
        {
            auto& cmd = commandList->getCommand<commands::SetRayTracingState>(command);
            RayTracingPipeline* pipeline = checked_cast<RayTracingPipeline*>(cmd.pipeline);
            auto specializationArgs = static_cast<ExtendedShaderObjectTypeListObject*>(cmd.specializationArgs);
            Pipeline* concretePipeline = nullptr;
            SLANG_RETURN_ON_FAIL(device->getConcretePipeline(pipeline, specializationArgs, concretePipeline));
            cmd.pipeline = static_cast<RayTracingPipeline*>(concretePipeline);
            cmd.specializationArgs = nullptr;
        }
        command = command->next;
    }
    return SLANG_OK;
}

// ----------------------------------------------------------------------------
// CommandBuffer
// ----------------------------------------------------------------------------

ICommandBuffer* CommandBuffer::getInterface(const Guid& guid)
{
    if (guid == ISlangUnknown::getTypeGuid() || guid == ICommandBuffer::getTypeGuid())
        return static_cast<ICommandBuffer*>(this);
    return nullptr;
}

} // namespace rhi
