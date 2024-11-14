#include "command-list.h"

namespace rhi {

CommandList::CommandList() = default;

CommandList::~CommandList()
{
    reset();
}

void CommandList::reset()
{
    releaseResources();
    m_commandSlots = nullptr;
    m_lastCommandSlot = nullptr;
    m_resourceSlots = nullptr;
    m_allocator.reset();
}

void CommandList::write(commands::CopyBuffer&& cmd)
{
    retainResource(cmd.dst);
    retainResource(cmd.src);
    writeCommand(std::move(cmd));
}

void CommandList::write(commands::CopyTexture&& cmd)
{
    retainResource(cmd.dst);
    retainResource(cmd.src);
    writeCommand(std::move(cmd));
}

void CommandList::write(commands::CopyTextureToBuffer&& cmd)
{
    retainResource(cmd.dst);
    retainResource(cmd.src);
    writeCommand(std::move(cmd));
}

void CommandList::write(commands::ClearBuffer&& cmd)
{
    retainResource(cmd.buffer);
    writeCommand(std::move(cmd));
}

void CommandList::write(commands::ClearTexture&& cmd)
{
    retainResource(cmd.texture);
    writeCommand(std::move(cmd));
}

void CommandList::write(commands::UploadTextureData&& cmd)
{
    retainResource(cmd.dst);
    if (cmd.subresourceData && cmd.subresourceDataCount > 0)
    {
        cmd.subresourceData =
            (SubresourceData*)writeData(cmd.subresourceData, cmd.subresourceDataCount * sizeof(SubresourceData));
        // TODO
        // for (Index i = 0; i < cmd.subresourceDataCount; ++i)
        //     cmd.subresourceData[i].data = writeData(cmd.subresourceData[i].data, cmd.subresourceData[i].size);
    }
    writeCommand(std::move(cmd));
}

void CommandList::write(commands::UploadBufferData&& cmd)
{
    retainResource(cmd.dst);
    if (cmd.data)
        cmd.data = writeData(cmd.data, cmd.size);
    writeCommand(std::move(cmd));
}

void CommandList::write(commands::ResolveQuery&& cmd)
{
    retainResource(cmd.queryPool);
    retainResource(cmd.buffer);
    writeCommand(std::move(cmd));
}

void CommandList::write(commands::BeginRenderPass&& cmd)
{
    if (cmd.desc.colorAttachments && cmd.desc.colorAttachmentCount > 0)
    {
        cmd.desc.colorAttachments = (RenderPassColorAttachment*)
            writeData(cmd.desc.colorAttachments, cmd.desc.colorAttachmentCount * sizeof(RenderPassColorAttachment));
        for (Index i = 0; i < cmd.desc.colorAttachmentCount; ++i)
        {
            retainResource(cmd.desc.colorAttachments[i].view);
            retainResource(cmd.desc.colorAttachments[i].resolveTarget);
        }
    }
    if (cmd.desc.depthStencilAttachment)
    {
        cmd.desc.depthStencilAttachment = (RenderPassDepthStencilAttachment*)
            writeData(cmd.desc.depthStencilAttachment, sizeof(RenderPassDepthStencilAttachment));
        retainResource(cmd.desc.depthStencilAttachment->view);
    }
    writeCommand(std::move(cmd));
}

void CommandList::write(commands::EndRenderPass&& cmd)
{
    writeCommand(std::move(cmd));
}

void CommandList::write(commands::SetRenderState&& cmd)
{
    retainResource(cmd.state.pipeline);
    retainResource(cmd.state.rootObject);
    for (Index i = 0; i < cmd.state.vertexBufferCount; ++i)
        retainResource(cmd.state.vertexBuffers[i].buffer);
    retainResource(cmd.state.indexBuffer.buffer);
    writeCommand(std::move(cmd));
}

void CommandList::write(commands::Draw&& cmd)
{
    writeCommand(std::move(cmd));
}

void CommandList::write(commands::DrawIndexed&& cmd)
{
    writeCommand(std::move(cmd));
}

void CommandList::write(commands::DrawIndirect&& cmd)
{
    retainResource(cmd.argBuffer);
    retainResource(cmd.countBuffer);
    writeCommand(std::move(cmd));
}

void CommandList::write(commands::DrawIndexedIndirect&& cmd)
{
    retainResource(cmd.argBuffer);
    retainResource(cmd.countBuffer);
    writeCommand(std::move(cmd));
}

void CommandList::write(commands::DrawMeshTasks&& cmd)
{
    writeCommand(std::move(cmd));
}

void CommandList::write(commands::BeginComputePass&& cmd)
{
    writeCommand(std::move(cmd));
}

void CommandList::write(commands::EndComputePass&& cmd)
{
    writeCommand(std::move(cmd));
}

void CommandList::write(commands::SetComputeState&& cmd)
{
    retainResource(cmd.state.pipeline);
    retainResource(cmd.state.rootObject);
    writeCommand(std::move(cmd));
}

void CommandList::write(commands::DispatchCompute&& cmd)
{
    writeCommand(std::move(cmd));
}

void CommandList::write(commands::DispatchComputeIndirect&& cmd)
{
    retainResource(cmd.argBuffer);
    writeCommand(std::move(cmd));
}

void CommandList::write(commands::BeginRayTracingPass&& cmd)
{
    writeCommand(std::move(cmd));
}

void CommandList::write(commands::EndRayTracingPass&& cmd)
{
    writeCommand(std::move(cmd));
}

void CommandList::write(commands::SetRayTracingState&& cmd)
{
    retainResource(cmd.state.pipeline);
    retainResource(cmd.state.shaderTable);
    retainResource(cmd.state.rootObject);
    writeCommand(std::move(cmd));
}

void CommandList::write(commands::DispatchRays&& cmd)
{
    writeCommand(std::move(cmd));
}

void CommandList::write(commands::BuildAccelerationStructure&& cmd)
{
    if (cmd.desc.inputs && cmd.desc.inputCount > 0)
    {
        AccelerationStructureBuildInputType type = *(AccelerationStructureBuildInputType*)cmd.desc.inputs;
        switch (type)
        {
        case AccelerationStructureBuildInputType::Instances:
        {
            AccelerationStructureBuildInputInstances* inputs = (AccelerationStructureBuildInputInstances*)
                writeData(cmd.desc.inputs, cmd.desc.inputCount * sizeof(AccelerationStructureBuildInputInstances));
            cmd.desc.inputs = inputs;
            for (Index i = 0; i < cmd.desc.inputCount; ++i)
            {
                retainResource(inputs[i].instanceBuffer.buffer);
            }
            break;
        }
        case AccelerationStructureBuildInputType::Triangles:
        {
            AccelerationStructureBuildInputTriangles* inputs = (AccelerationStructureBuildInputTriangles*)
                writeData(cmd.desc.inputs, cmd.desc.inputCount * sizeof(AccelerationStructureBuildInputTriangles));
            cmd.desc.inputs = inputs;
            for (Index i = 0; i < cmd.desc.inputCount; ++i)
            {
                for (Index j = 0; j < inputs[i].vertexBufferCount; ++j)
                    retainResource(inputs[i].vertexBuffers[j].buffer);
                retainResource(inputs[i].indexBuffer.buffer);
                retainResource(inputs[i].preTransformBuffer.buffer);
            }
            break;
        }
        case AccelerationStructureBuildInputType::ProceduralPrimitives:
        {
            AccelerationStructureBuildInputProceduralPrimitives* inputs =
                (AccelerationStructureBuildInputProceduralPrimitives*)writeData(
                    cmd.desc.inputs,
                    cmd.desc.inputCount * sizeof(AccelerationStructureBuildInputProceduralPrimitives)
                );
            cmd.desc.inputs = inputs;
            for (Index i = 0; i < cmd.desc.inputCount; ++i)
            {
                for (Index j = 0; j < inputs[i].aabbBufferCount; ++j)
                    retainResource(inputs[i].aabbBuffers[j].buffer);
            }
            break;
        }
        }
    }
    retainResource(cmd.dst);
    retainResource(cmd.src);
    retainResource(cmd.scratchBuffer.buffer);
    if (cmd.queryDescs && cmd.propertyQueryCount > 0)
    {
        cmd.queryDescs = (AccelerationStructureQueryDesc*)
            writeData(cmd.queryDescs, cmd.propertyQueryCount * sizeof(AccelerationStructureQueryDesc));
        for (Index i = 0; i < cmd.propertyQueryCount; ++i)
            retainResource(cmd.queryDescs[i].queryPool);
    }
    writeCommand(std::move(cmd));
}

void CommandList::write(commands::CopyAccelerationStructure&& cmd)
{
    retainResource(cmd.dst);
    retainResource(cmd.src);
    writeCommand(std::move(cmd));
}

void CommandList::write(commands::QueryAccelerationStructureProperties&& cmd)
{
    if (cmd.accelerationStructures && cmd.accelerationStructureCount > 0)
    {
        cmd.accelerationStructures = (IAccelerationStructure**)
            writeData(cmd.accelerationStructures, cmd.accelerationStructureCount * sizeof(IAccelerationStructure*));
        for (Index i = 0; i < cmd.accelerationStructureCount; ++i)
            retainResource(cmd.accelerationStructures[i]);
    }
    if (cmd.queryDescs && cmd.queryCount > 0)
    {
        cmd.queryDescs = (AccelerationStructureQueryDesc*)
            writeData(cmd.queryDescs, cmd.queryCount * sizeof(AccelerationStructureQueryDesc));
        for (Index i = 0; i < cmd.queryCount; ++i)
            retainResource(cmd.queryDescs[i].queryPool);
    }
    writeCommand(std::move(cmd));
}

void CommandList::write(commands::SerializeAccelerationStructure&& cmd)
{
    retainResource(cmd.dst.buffer);
    retainResource(cmd.src);
    writeCommand(std::move(cmd));
}

void CommandList::write(commands::DeserializeAccelerationStructure&& cmd)
{
    retainResource(cmd.dst);
    retainResource(cmd.src.buffer);
    writeCommand(std::move(cmd));
}

void CommandList::write(commands::SetBufferState&& cmd)
{
    retainResource(cmd.buffer);
    writeCommand(std::move(cmd));
}

void CommandList::write(commands::SetTextureState&& cmd)
{
    retainResource(cmd.texture);
    writeCommand(std::move(cmd));
}

void CommandList::write(commands::PushDebugGroup&& cmd)
{
    if (cmd.name)
        cmd.name = (const char*)writeData(cmd.name, strlen(cmd.name) + 1);
    writeCommand(std::move(cmd));
}

void CommandList::write(commands::PopDebugGroup&& cmd)
{
    writeCommand(std::move(cmd));
}

void CommandList::write(commands::InsertDebugMarker&& cmd)
{
    if (cmd.name)
        cmd.name = (const char*)writeData(cmd.name, strlen(cmd.name) + 1);
    writeCommand(std::move(cmd));
}

void CommandList::write(commands::WriteTimestamp&& cmd)
{
    retainResource(cmd.queryPool);
    writeCommand(std::move(cmd));
}

void CommandList::write(commands::ExecuteCallback&& cmd)
{
    if (cmd.userData && cmd.userDataSize > 0)
        cmd.userData = writeData(cmd.userData, cmd.userDataSize);
    writeCommand(std::move(cmd));
}

} // namespace rhi
