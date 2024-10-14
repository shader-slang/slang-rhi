#pragma once

// Provides shared implementation of public API objects for targets with
// an immediate mode execution context.

#include "rhi-shared.h"

namespace rhi {

enum class MapFlavor
{
    /// Unknown mapping type.
    Unknown,
    HostRead,
    HostWrite,
    WriteDiscard,
};

class ImmediateDevice;
class ImmediateCommandQueueBase : public CommandQueue<ImmediateDevice>
{
public:
    ImmediateCommandQueueBase(ImmediateDevice* device, QueueType type)
        : CommandQueue(device, type)
    {
    }
};

struct CommandBufferInfo
{
    bool hasWriteTimestamps;
};

class ImmediateDevice : public Device
{
public:
    // Immediate commands to be implemented by each target.
    virtual Result createRootShaderObject(IShaderProgram* program, ShaderObjectBase** outObject) = 0;
    virtual void beginRenderPass(const RenderPassDesc& desc) = 0;
    virtual void endRenderPass() = 0;
    virtual void setRenderState(const RenderState& state) = 0;
    virtual void draw(const DrawArguments& args) = 0;
    virtual void drawIndexed(const DrawArguments& args) = 0;
    virtual void setComputeState(const ComputeState& state) = 0;
    virtual void dispatchCompute(int x, int y, int z) = 0;
    virtual void copyBuffer(IBuffer* dst, Offset dstOffset, IBuffer* src, Offset srcOffset, Size size) = 0;
    virtual void submitGpuWork() = 0;
    virtual void waitForGpu() = 0;
    virtual void* map(IBuffer* buffer, MapFlavor flavor) = 0;
    virtual void unmap(IBuffer* buffer, size_t offsetWritten, size_t sizeWritten) = 0;
    virtual void writeTimestamp(IQueryPool* pool, GfxIndex index) = 0;
    virtual void beginCommandBuffer(const CommandBufferInfo&) {}
    virtual void endCommandBuffer(const CommandBufferInfo&) {}

public:
    RefPtr<ImmediateCommandQueueBase> m_queue;

    ImmediateDevice();

    virtual SLANG_NO_THROW Result SLANG_MCALL getQueue(QueueType type, ICommandQueue** outQueue) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    createTransientResourceHeap(const ITransientResourceHeap::Desc& desc, ITransientResourceHeap** outHeap) override;

    void uploadBufferData(IBuffer* dst, Offset offset, Size size, void* data);

    virtual SLANG_NO_THROW Result SLANG_MCALL
    readBuffer(IBuffer* buffer, Offset offset, Size size, ISlangBlob** outBlob) override;
};

class ImmediateComputeDeviceBase : public ImmediateDevice
{
public:
    // Provide empty implementation for devices without graphics support.
    virtual void beginRenderPass(const RenderPassDesc& desc) override { SLANG_UNUSED(desc); }
    virtual void endRenderPass() override {}
    virtual void setRenderState(const RenderState& state) override { SLANG_UNUSED(state); }
    virtual void draw(const DrawArguments& args) override { SLANG_UNUSED(args); }
    virtual void drawIndexed(const DrawArguments& args) override { SLANG_UNUSED(args); }

    virtual SLANG_NO_THROW Result SLANG_MCALL
    createInputLayout(InputLayoutDesc const& desc, IInputLayout** outLayout) override
    {
        SLANG_UNUSED(desc);
        SLANG_UNUSED(outLayout);
        return SLANG_E_NOT_AVAILABLE;
    }
    virtual SLANG_NO_THROW Result SLANG_MCALL
    readTexture(ITexture* texture, ISlangBlob** outBlob, Size* outRowPitch, Size* outPixelSize) override
    {
        SLANG_UNUSED(texture);
        SLANG_UNUSED(outBlob);
        SLANG_UNUSED(outRowPitch);
        SLANG_UNUSED(outPixelSize);

        return SLANG_E_NOT_AVAILABLE;
    }
};
} // namespace rhi
