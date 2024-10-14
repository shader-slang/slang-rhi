#pragma once

#include <slang-rhi.h>

#include "rhi-shared.h"

#include "core/common.h"

#include <vector>

namespace rhi {

enum class CommandName
{
    CopyBuffer,
    UploadBufferData,
    BeginRenderPass,
    EndRenderPass,
    SetRenderState,
    Draw,
    DrawIndexed,
    SetComputeState,
    DispatchCompute,
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

    void copyBuffer(IBuffer* dst, Offset dstOffset, IBuffer* src, Offset srcOffset, Size size)
    {
        auto dstBuffer = encodeObject(checked_cast<Buffer*>(dst));
        auto srcBuffer = encodeObject(checked_cast<Buffer*>(src));
        m_commands.push_back(Command(
            CommandName::CopyBuffer,
            (uint32_t)dstBuffer,
            (uint32_t)dstOffset,
            (uint32_t)srcBuffer,
            (uint32_t)srcOffset,
            (uint32_t)size
        ));
    }

    void uploadBufferData(IBuffer* buffer, Offset offset, Size size, void* data)
    {
        auto bufferOffset = encodeObject(checked_cast<Buffer*>(buffer));
        auto dataOffset = encodeData(data, size);
        m_commands.push_back(Command(
            CommandName::UploadBufferData,
            (uint32_t)bufferOffset,
            (uint32_t)offset,
            (uint32_t)size,
            (uint32_t)dataOffset
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
        for (uint32_t i = 0; i < desc.colorAttachmentCount; i++)
        {
            encodeObject(checked_cast<TextureView*>(desc.colorAttachments[i].view));
            encodeObject(checked_cast<TextureView*>(desc.colorAttachments[i].resolveTarget));
        }
        if (desc.depthStencilAttachment)
        {
            encodeObject(checked_cast<TextureView*>(desc.depthStencilAttachment->view));
        }
        m_commands.push_back(Command(
            CommandName::BeginRenderPass,
            (uint32_t)desc.colorAttachmentCount,
            (uint32_t)(desc.depthStencilAttachment ? 1 : 0),
            (uint32_t)colorAttachmentsOffset,
            (uint32_t)depthStencilAttachmentOffset
        ));
    }

    void endRenderPass() { m_commands.push_back(Command(CommandName::EndRenderPass)); }

    void setRenderState(const RenderState& state)
    {
        Offset offset = encodeData(&state, sizeof(state));
        encodeObject(checked_cast<RenderPipeline*>(state.pipeline));
        encodeObject(checked_cast<ShaderObjectBase*>(state.rootObject));
        for (Index i = 0; i < state.vertexBufferCount; i++)
        {
            encodeObject(checked_cast<Buffer*>(state.vertexBuffers[i].buffer));
        }
        encodeObject(checked_cast<Buffer*>(state.indexBuffer.buffer));
        m_commands.push_back(Command(CommandName::SetRenderState, (uint32_t)offset));
    }

    void draw(const DrawArguments& args)
    {
        Offset offset = encodeData(&args, sizeof(args));
        m_commands.push_back(Command(CommandName::Draw, (uint32_t)offset));
    }

    void drawIndexed(const DrawArguments& args)
    {
        Offset offset = encodeData(&args, sizeof(args));
        m_commands.push_back(Command(CommandName::DrawIndexed, (uint32_t)offset));
    }

    void setComputeState(const ComputeState& state)
    {
        Offset offset = encodeData(&state, sizeof(state));
        encodeObject(checked_cast<ComputePipeline*>(state.pipeline));
        encodeObject(checked_cast<ShaderObjectBase*>(state.rootObject));
        m_commands.push_back(Command(CommandName::SetComputeState, (uint32_t)offset));
    }

    void dispatchCompute(int x, int y, int z)
    {
        m_commands.push_back(Command(CommandName::DispatchCompute, (uint32_t)x, (uint32_t)y, (uint32_t)z));
    }

    void writeTimestamp(IQueryPool* pool, GfxIndex index)
    {
        auto poolOffset = encodeObject(checked_cast<QueryPool*>(pool));
        m_commands.push_back(Command(CommandName::WriteTimestamp, (uint32_t)poolOffset, (uint32_t)index));
        m_hasWriteTimestamps = true;
    }
};
} // namespace rhi
