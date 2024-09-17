#pragma once

#include <slang-rhi.h>

#include "renderer-shared.h"

#include "core/common.h"

#include <vector>

namespace rhi {

enum class CommandName
{
    SetPipeline,
    BindRootShaderObject,
    BeginRenderPass,
    EndRenderPass,
    SetViewports,
    SetScissorRects,
    SetPrimitiveTopology,
    SetVertexBuffers,
    SetIndexBuffer,
    Draw,
    DrawIndexed,
    DrawInstanced,
    DrawIndexedInstanced,
    SetStencilReference,
    DispatchCompute,
    UploadBufferData,
    CopyBuffer,
    WriteTimestamp,
};

const uint8_t kMaxCommandOperands = 5;

struct Command
{
    CommandName name;
    uint32_t operands[kMaxCommandOperands];
    Command() = default;
    Command(CommandName inName)
        : name(inName)
    {
    }
    Command(CommandName inName, uint32_t op)
        : name(inName)
    {
        operands[0] = op;
    }
    Command(CommandName inName, uint32_t op1, uint32_t op2)
        : name(inName)
    {
        operands[0] = op1;
        operands[1] = op2;
    }
    Command(CommandName inName, uint32_t op1, uint32_t op2, uint32_t op3)
        : name(inName)
    {
        operands[0] = op1;
        operands[1] = op2;
        operands[2] = op3;
    }
    Command(CommandName inName, uint32_t op1, uint32_t op2, uint32_t op3, uint32_t op4)
        : name(inName)
    {
        operands[0] = op1;
        operands[1] = op2;
        operands[2] = op3;
        operands[3] = op4;
    }
    Command(CommandName inName, uint32_t op1, uint32_t op2, uint32_t op3, uint32_t op4, uint32_t op5)
        : name(inName)
    {
        operands[0] = op1;
        operands[1] = op2;
        operands[2] = op3;
        operands[3] = op4;
        operands[4] = op5;
    }
};

class CommandWriter
{
public:
    std::vector<Command> m_commands;
    std::vector<RefPtr<RefObject>> m_objects;
    std::vector<uint8_t> m_data;
    bool m_hasWriteTimestamps = false;

public:
    void clear()
    {
        m_commands.clear();
        for (auto& obj : m_objects)
            obj = nullptr;
        m_objects.clear();
        m_data.clear();
        m_hasWriteTimestamps = false;
    }

    // Copies user data into `m_data` buffer and returns the offset to retrieve the data.
    Offset encodeData(const void* data, Size size)
    {
        Offset offset = (Offset)m_data.size();
        m_data.resize(m_data.size() + size);
        memcpy(m_data.data() + offset, data, size);
        return offset;
    }

    Offset encodeObject(RefObject* obj)
    {
        Offset offset = (Offset)m_objects.size();
        m_objects.push_back(obj);
        return offset;
    }

    template<typename T>
    T* getObject(uint32_t offset)
    {
        return static_cast<T*>(m_objects[offset].Ptr());
    }

    template<typename T>
    T* getData(Offset offset)
    {
        return reinterpret_cast<T*>(m_data.data() + offset);
    }

    void setPipeline(IPipeline* state)
    {
        auto offset = encodeObject(static_cast<Pipeline*>(state));
        m_commands.push_back(Command(CommandName::SetPipeline, (uint32_t)offset));
    }

    void bindRootShaderObject(IShaderObject* object)
    {
        auto rootOffset = encodeObject(static_cast<ShaderObjectBase*>(object));
        m_commands.push_back(Command(CommandName::BindRootShaderObject, (uint32_t)rootOffset));
    }

    void uploadBufferData(IBuffer* buffer, Offset offset, Size size, void* data)
    {
        auto bufferOffset = encodeObject(static_cast<Buffer*>(buffer));
        auto dataOffset = encodeData(data, size);
        m_commands.push_back(Command(
            CommandName::UploadBufferData,
            (uint32_t)bufferOffset,
            (uint32_t)offset,
            (uint32_t)size,
            (uint32_t)dataOffset
        ));
    }

    void copyBuffer(IBuffer* dst, Offset dstOffset, IBuffer* src, Offset srcOffset, Size size)
    {
        auto dstBuffer = encodeObject(static_cast<Buffer*>(dst));
        auto srcBuffer = encodeObject(static_cast<Buffer*>(src));
        m_commands.push_back(Command(
            CommandName::CopyBuffer,
            (uint32_t)dstBuffer,
            (uint32_t)dstOffset,
            (uint32_t)srcBuffer,
            (uint32_t)srcOffset,
            (uint32_t)size
        ));
    }

    void beginRenderPass(const RenderPassDesc& desc)
    {
        Offset colorAttachmentsOffset =
            encodeData(desc.colorAttachments, sizeof(RenderPassColorAttachment) * desc.colorAttachmentCount);
        Offset depthStencilAttachmentOffset = encodeData(
            desc.depthStencilAttachment,
            desc.depthStencilAttachment ? sizeof(RenderPassDepthStencilAttachment) : 0
        );
        Offset viewsOffset = 0;
        for (uint32_t i = 0; i < desc.colorAttachmentCount; i++)
        {
            auto offset = encodeObject(static_cast<TextureView*>(desc.colorAttachments[i].view));
            if (i == 0)
                viewsOffset = offset;
        }
        if (desc.depthStencilAttachment)
        {
            auto offset = encodeObject(static_cast<TextureView*>(desc.depthStencilAttachment->view));
            if (desc.colorAttachmentCount == 0)
                viewsOffset = offset;
        }
        m_commands.push_back(Command(
            CommandName::BeginRenderPass,
            (uint32_t)desc.colorAttachmentCount,
            (uint32_t)(desc.depthStencilAttachment ? 1 : 0),
            (uint32_t)colorAttachmentsOffset,
            (uint32_t)depthStencilAttachmentOffset,
            (uint32_t)viewsOffset
        ));
    }

    void endRenderPass() { m_commands.push_back(Command(CommandName::EndRenderPass)); }

    void setViewports(GfxCount count, const Viewport* viewports)
    {
        auto offset = encodeData(viewports, sizeof(Viewport) * count);
        m_commands.push_back(Command(CommandName::SetViewports, (uint32_t)count, (uint32_t)offset));
    }

    void setScissorRects(GfxCount count, const ScissorRect* scissors)
    {
        auto offset = encodeData(scissors, sizeof(ScissorRect) * count);
        m_commands.push_back(Command(CommandName::SetScissorRects, (uint32_t)count, (uint32_t)offset));
    }

    void setPrimitiveTopology(PrimitiveTopology topology)
    {
        m_commands.push_back(Command(CommandName::SetPrimitiveTopology, (uint32_t)topology));
    }

    void setVertexBuffers(GfxIndex startSlot, GfxCount slotCount, IBuffer* const* buffers, const Offset* offsets)
    {
        Offset bufferOffset = 0;
        for (GfxCount i = 0; i < slotCount; i++)
        {
            auto offset = encodeObject(static_cast<Buffer*>(buffers[i]));
            if (i == 0)
                bufferOffset = offset;
        }
        auto offsetsOffset = encodeData(offsets, sizeof(Size) * slotCount);
        m_commands.push_back(Command(
            CommandName::SetVertexBuffers,
            (uint32_t)startSlot,
            (uint32_t)slotCount,
            (uint32_t)bufferOffset,
            (uint32_t)offsetsOffset
        ));
    }

    void setIndexBuffer(IBuffer* buffer, Format indexFormat, Offset offset)
    {
        auto bufferOffset = encodeObject(static_cast<Buffer*>(buffer));
        m_commands.push_back(
            Command(CommandName::SetIndexBuffer, (uint32_t)bufferOffset, (uint32_t)indexFormat, (uint32_t)offset)
        );
    }

    void draw(GfxCount vertexCount, GfxIndex startVertex)
    {
        m_commands.push_back(Command(CommandName::Draw, (uint32_t)vertexCount, (uint32_t)startVertex));
    }

    void drawIndexed(GfxCount indexCount, GfxIndex startIndex, GfxIndex baseVertex)
    {
        m_commands.push_back(
            Command(CommandName::DrawIndexed, (uint32_t)indexCount, (uint32_t)startIndex, (uint32_t)baseVertex)
        );
    }

    void drawInstanced(
        GfxCount vertexCount,
        GfxCount instanceCount,
        GfxIndex startVertex,
        GfxIndex startInstanceLocation
    )
    {
        m_commands.push_back(Command(
            CommandName::DrawInstanced,
            (uint32_t)vertexCount,
            (uint32_t)instanceCount,
            (uint32_t)startVertex,
            (uint32_t)startInstanceLocation
        ));
    }

    void drawIndexedInstanced(
        GfxCount indexCount,
        GfxCount instanceCount,
        GfxIndex startIndexLocation,
        GfxIndex baseVertexLocation,
        GfxIndex startInstanceLocation
    )
    {
        m_commands.push_back(Command(
            CommandName::DrawIndexedInstanced,
            (uint32_t)indexCount,
            (uint32_t)instanceCount,
            (uint32_t)startIndexLocation,
            (uint32_t)baseVertexLocation,
            (uint32_t)startInstanceLocation
        ));
    }

    void setStencilReference(uint32_t referenceValue)
    {
        m_commands.push_back(Command(CommandName::SetStencilReference, referenceValue));
    }

    void dispatchCompute(int x, int y, int z)
    {
        m_commands.push_back(Command(CommandName::DispatchCompute, (uint32_t)x, (uint32_t)y, (uint32_t)z));
    }

    void writeTimestamp(IQueryPool* pool, GfxIndex index)
    {
        auto poolOffset = encodeObject(static_cast<QueryPool*>(pool));
        m_commands.push_back(Command(CommandName::WriteTimestamp, (uint32_t)poolOffset, (uint32_t)index));
        m_hasWriteTimestamps = true;
    }
};
} // namespace rhi
