#include "command-list.h"
#include "rhi-shared.h"

namespace rhi {

CommandList::CommandList(ArenaAllocator& allocator, std::set<RefPtr<RefObject>>& trackedObjects)
    : m_allocator(allocator)
    , m_trackedObjects(trackedObjects)
{
}

void CommandList::reset()
{
    m_commandSlots = nullptr;
    m_lastCommandSlot = nullptr;
}

void CommandList::write(commands::CopyBuffer&& cmd)
{
    retainResource<Buffer>(cmd.dst);
    retainResource<Buffer>(cmd.src);
    writeCommand(std::move(cmd));
}

void CommandList::write(commands::CopyTexture&& cmd)
{
    retainResource<Texture>(cmd.dst);
    retainResource<Texture>(cmd.src);
    writeCommand(std::move(cmd));
}

void CommandList::write(commands::CopyTextureToBuffer&& cmd)
{
    retainResource<Buffer>(cmd.dst);
    retainResource<Texture>(cmd.src);
    writeCommand(std::move(cmd));
}

void CommandList::write(commands::ClearBuffer&& cmd)
{
    retainResource<Buffer>(cmd.buffer);
    writeCommand(std::move(cmd));
}

void CommandList::write(commands::ClearTexture&& cmd)
{
    retainResource<Texture>(cmd.texture);
    writeCommand(std::move(cmd));
}

void CommandList::write(commands::UploadTextureData&& cmd)
{
    retainResource<Texture>(cmd.dst);
    if (cmd.subresourceData && cmd.subresourceDataCount > 0)
    {
        cmd.subresourceData =
            (SubresourceData*)writeData(cmd.subresourceData, cmd.subresourceDataCount * sizeof(SubresourceData));
        // TODO
        // for (uint32_t i = 0; i < cmd.subresourceDataCount; ++i)
        //     cmd.subresourceData[i].data = writeData(cmd.subresourceData[i].data, cmd.subresourceData[i].size);
    }
    writeCommand(std::move(cmd));
}

void CommandList::write(commands::UploadBufferData&& cmd)
{
    retainResource<Buffer>(cmd.dst);
    if (cmd.data)
        cmd.data = writeData(cmd.data, cmd.size);
    writeCommand(std::move(cmd));
}

void CommandList::write(commands::ResolveQuery&& cmd)
{
    retainResource<QueryPool>(cmd.queryPool);
    retainResource<Buffer>(cmd.buffer);
    writeCommand(std::move(cmd));
}

void CommandList::write(commands::BeginRenderPass&& cmd)
{
    if (cmd.desc.colorAttachments && cmd.desc.colorAttachmentCount > 0)
    {
        cmd.desc.colorAttachments = (RenderPassColorAttachment*)
            writeData(cmd.desc.colorAttachments, cmd.desc.colorAttachmentCount * sizeof(RenderPassColorAttachment));
        for (uint32_t i = 0; i < cmd.desc.colorAttachmentCount; ++i)
        {
            retainResource<TextureView>(cmd.desc.colorAttachments[i].view);
            retainResource<TextureView>(cmd.desc.colorAttachments[i].resolveTarget);
        }
    }
    if (cmd.desc.depthStencilAttachment)
    {
        cmd.desc.depthStencilAttachment = (RenderPassDepthStencilAttachment*)
            writeData(cmd.desc.depthStencilAttachment, sizeof(RenderPassDepthStencilAttachment));
        retainResource<TextureView>(cmd.desc.depthStencilAttachment->view);
    }
    writeCommand(std::move(cmd));
}

void CommandList::write(commands::EndRenderPass&& cmd)
{
    writeCommand(std::move(cmd));
}

void CommandList::write(commands::SetRenderState&& cmd)
{
    for (uint32_t i = 0; i < cmd.state.vertexBufferCount; ++i)
        retainResource<Buffer>(cmd.state.vertexBuffers[i].buffer);
    retainResource<Buffer>(cmd.state.indexBuffer.buffer);
    retainResource<RenderPipeline>(cmd.pipeline);
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
    retainResource<Buffer>(cmd.argBuffer);
    retainResource<Buffer>(cmd.countBuffer);
    writeCommand(std::move(cmd));
}

void CommandList::write(commands::DrawIndexedIndirect&& cmd)
{
    retainResource<Buffer>(cmd.argBuffer);
    retainResource<Buffer>(cmd.countBuffer);
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
    retainResource<ComputePipeline>(cmd.pipeline);
    writeCommand(std::move(cmd));
}

void CommandList::write(commands::DispatchCompute&& cmd)
{
    writeCommand(std::move(cmd));
}

void CommandList::write(commands::DispatchComputeIndirect&& cmd)
{
    retainResource<Buffer>(cmd.argBuffer);
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
    retainResource<RayTracingPipeline>(cmd.pipeline);
    retainResource<ShaderTable>(cmd.shaderTable);
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
            for (uint32_t i = 0; i < cmd.desc.inputCount; ++i)
            {
                retainResource<Buffer>(inputs[i].instanceBuffer.buffer);
            }
            break;
        }
        case AccelerationStructureBuildInputType::Triangles:
        {
            AccelerationStructureBuildInputTriangles* inputs = (AccelerationStructureBuildInputTriangles*)
                writeData(cmd.desc.inputs, cmd.desc.inputCount * sizeof(AccelerationStructureBuildInputTriangles));
            cmd.desc.inputs = inputs;
            for (uint32_t i = 0; i < cmd.desc.inputCount; ++i)
            {
                for (uint32_t j = 0; j < inputs[i].vertexBufferCount; ++j)
                    retainResource<Buffer>(inputs[i].vertexBuffers[j].buffer);
                retainResource<Buffer>(inputs[i].indexBuffer.buffer);
                retainResource<Buffer>(inputs[i].preTransformBuffer.buffer);
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
            for (uint32_t i = 0; i < cmd.desc.inputCount; ++i)
            {
                for (uint32_t j = 0; j < inputs[i].aabbBufferCount; ++j)
                    retainResource<Buffer>(inputs[i].aabbBuffers[j].buffer);
            }
            break;
        }
        }
    }
    retainResource<AccelerationStructure>(cmd.dst);
    retainResource<AccelerationStructure>(cmd.src);
    retainResource<Buffer>(cmd.scratchBuffer.buffer);
    if (cmd.queryDescs && cmd.propertyQueryCount > 0)
    {
        cmd.queryDescs = (AccelerationStructureQueryDesc*)
            writeData(cmd.queryDescs, cmd.propertyQueryCount * sizeof(AccelerationStructureQueryDesc));
        for (uint32_t i = 0; i < cmd.propertyQueryCount; ++i)
            retainResource<QueryPool>(cmd.queryDescs[i].queryPool);
    }
    writeCommand(std::move(cmd));
}

void CommandList::write(commands::CopyAccelerationStructure&& cmd)
{
    retainResource<AccelerationStructure>(cmd.dst);
    retainResource<AccelerationStructure>(cmd.src);
    writeCommand(std::move(cmd));
}

void CommandList::write(commands::QueryAccelerationStructureProperties&& cmd)
{
    if (cmd.accelerationStructures && cmd.accelerationStructureCount > 0)
    {
        cmd.accelerationStructures = (IAccelerationStructure**)
            writeData(cmd.accelerationStructures, cmd.accelerationStructureCount * sizeof(IAccelerationStructure*));
        for (uint32_t i = 0; i < cmd.accelerationStructureCount; ++i)
            retainResource<AccelerationStructure>(cmd.accelerationStructures[i]);
    }
    if (cmd.queryDescs && cmd.queryCount > 0)
    {
        cmd.queryDescs = (AccelerationStructureQueryDesc*)
            writeData(cmd.queryDescs, cmd.queryCount * sizeof(AccelerationStructureQueryDesc));
        for (uint32_t i = 0; i < cmd.queryCount; ++i)
            retainResource<QueryPool>(cmd.queryDescs[i].queryPool);
    }
    writeCommand(std::move(cmd));
}

void CommandList::write(commands::SerializeAccelerationStructure&& cmd)
{
    retainResource<Buffer>(cmd.dst.buffer);
    retainResource<AccelerationStructure>(cmd.src);
    writeCommand(std::move(cmd));
}

void CommandList::write(commands::DeserializeAccelerationStructure&& cmd)
{
    retainResource<AccelerationStructure>(cmd.dst);
    retainResource<Buffer>(cmd.src.buffer);
    writeCommand(std::move(cmd));
}

void CommandList::write(commands::ConvertCooperativeVectorMatrix&& cmd)
{
    if (cmd.descs && cmd.descCount > 0)
    {
        cmd.descs = (ConvertCooperativeVectorMatrixDesc*)
            writeData(cmd.descs, cmd.descCount * sizeof(ConvertCooperativeVectorMatrixDesc));
    }
    writeCommand(std::move(cmd));
}

void CommandList::write(commands::SetBufferState&& cmd)
{
    retainResource<Buffer>(cmd.buffer);
    writeCommand(std::move(cmd));
}

void CommandList::write(commands::SetTextureState&& cmd)
{
    retainResource<Texture>(cmd.texture);
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
    retainResource<QueryPool>(cmd.queryPool);
    writeCommand(std::move(cmd));
}

void CommandList::write(commands::ExecuteCallback&& cmd)
{
    if (cmd.userData && cmd.userDataSize > 0)
        cmd.userData = writeData(cmd.userData, cmd.userDataSize);
    writeCommand(std::move(cmd));
}

} // namespace rhi
