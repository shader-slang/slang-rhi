#include "immediate-device.h"
#include "command-writer.h"
#include "simple-transient-resource-heap.h"

#include "core/common.h"
#include "core/short_vector.h"

namespace rhi {

namespace {

class CommandBufferImpl : public ICommandBuffer, public CommandWriter, public ComObject
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL
    ICommandBuffer* getInterface(const Guid& guid)
    {
        if (guid == GUID::IID_ISlangUnknown || guid == GUID::IID_ICommandBuffer)
            return static_cast<ICommandBuffer*>(this);
        return nullptr;
    }

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override
    {
        *outHandle = {};
        return SLANG_E_NOT_AVAILABLE;
    }

public:
    void execute(ImmediateDevice* device)
    {
        for (auto& cmd : m_commands)
        {
            auto name = cmd.name;
            switch (name)
            {
            case CommandName::CopyBuffer:
                device->copyBuffer(
                    getObject<Buffer>(cmd.operands[0]),
                    cmd.operands[1],
                    getObject<Buffer>(cmd.operands[2]),
                    cmd.operands[3],
                    cmd.operands[4]
                );
                break;
            case CommandName::UploadBufferData:
                device->uploadBufferData(
                    getObject<Buffer>(cmd.operands[0]),
                    cmd.operands[1],
                    cmd.operands[2],
                    getData<uint8_t>(cmd.operands[3])
                );
                break;
            case CommandName::BeginRenderPass:
            {
                RenderPassDesc desc;
                if (cmd.operands[0] > 0)
                {
                    desc.colorAttachments = getData<RenderPassColorAttachment>(cmd.operands[2]);
                    desc.colorAttachmentCount = cmd.operands[0];
                }
                if (cmd.operands[1] > 0)
                {
                    desc.depthStencilAttachment = getData<RenderPassDepthStencilAttachment>(cmd.operands[3]);
                }
                device->beginRenderPass(desc);
                break;
            }
            case CommandName::EndRenderPass:
                device->endRenderPass();
                break;
            case CommandName::SetRenderState:
                device->setRenderState(*getData<RenderState>(cmd.operands[0]));
                break;
            case CommandName::Draw:
                device->draw(*getData<DrawArguments>(cmd.operands[0]));
                break;
            case CommandName::DrawIndexed:
                device->drawIndexed(*getData<DrawArguments>(cmd.operands[0]));
                break;
            case CommandName::SetComputeState:
                device->setComputeState(*getData<ComputeState>(cmd.operands[0]));
                break;
            case CommandName::DispatchCompute:
                device->dispatchCompute(int(cmd.operands[0]), int(cmd.operands[1]), int(cmd.operands[2]));
                break;
            case CommandName::WriteTimestamp:
                device->writeTimestamp(getObject<QueryPool>(cmd.operands[0]), (GfxIndex)cmd.operands[1]);
                break;
            default:
                SLANG_RHI_ASSERT_FAILURE("Unknown command");
                break;
            }
        }
        clear();
    }
};

class CommandEncoderImpl : public CommandEncoder
{
public:
    RefPtr<ImmediateDevice> m_device;
    RefPtr<ShaderObjectBase> m_rootShaderObject;
    TransientResourceHeap* m_transientHeap;
    RefPtr<CommandBufferImpl> m_commandBuffer;

    void init(ImmediateDevice* device, TransientResourceHeap* transientHeap)
    {
        m_device = device;
        m_transientHeap = transientHeap;
        m_commandBuffer = new CommandBufferImpl();
    }

    virtual Result createRootShaderObject(ShaderProgram* program, ShaderObjectBase** outObject) override
    {
        RefPtr<ShaderObjectBase> object;
        SLANG_RETURN_ON_FAIL(m_device->createRootShaderObject(program, object.writeRef()));
        // Root objects need to be kept alive until command buffer submission.
        m_commandBuffer->encodeObject(object);
        returnRefPtr(outObject, object);
        return SLANG_OK;
    }

    virtual SLANG_NO_THROW void SLANG_MCALL
    copyBuffer(IBuffer* dst, Offset dstOffset, IBuffer* src, Offset srcOffset, Size size) override
    {
        m_commandBuffer->copyBuffer(dst, dstOffset, src, srcOffset, size);
    }

    virtual SLANG_NO_THROW void SLANG_MCALL copyTexture(
        ITexture* dst,
        SubresourceRange dstSubresource,
        Offset3D dstOffset,
        ITexture* src,
        SubresourceRange srcSubresource,
        Offset3D srcOffset,
        Extents extent
    ) override
    {
        SLANG_UNUSED(dst);
        SLANG_UNUSED(dstSubresource);
        SLANG_UNUSED(dstOffset);
        SLANG_UNUSED(src);
        SLANG_UNUSED(srcSubresource);
        SLANG_UNUSED(srcOffset);
        SLANG_UNUSED(extent);
        SLANG_RHI_UNIMPLEMENTED("copyTexture");
    }

    virtual SLANG_NO_THROW void SLANG_MCALL copyTextureToBuffer(
        IBuffer* dst,
        Offset dstOffset,
        Size dstSize,
        Size dstRowStride,
        ITexture* src,
        SubresourceRange srcSubresource,
        Offset3D srcOffset,
        Extents extent
    ) override
    {
        SLANG_UNUSED(dst);
        SLANG_UNUSED(dstOffset);
        SLANG_UNUSED(dstSize);
        SLANG_UNUSED(dstRowStride);
        SLANG_UNUSED(src);
        SLANG_UNUSED(srcSubresource);
        SLANG_UNUSED(srcOffset);
        SLANG_UNUSED(extent);
        SLANG_RHI_UNIMPLEMENTED("copyTextureToBuffer");
    }

    virtual SLANG_NO_THROW void SLANG_MCALL uploadTextureData(
        ITexture* dst,
        SubresourceRange subresourceRange,
        Offset3D offset,
        Extents extent,
        SubresourceData* subresourceData,
        GfxCount subresourceDataCount
    ) override
    {
        SLANG_UNUSED(dst);
        SLANG_UNUSED(subresourceRange);
        SLANG_UNUSED(offset);
        SLANG_UNUSED(extent);
        SLANG_UNUSED(subresourceData);
        SLANG_UNUSED(subresourceDataCount);
        SLANG_RHI_UNIMPLEMENTED("uploadTextureData");
    }

    virtual SLANG_NO_THROW void SLANG_MCALL
    uploadBufferData(IBuffer* dst, Offset offset, Size size, void* data) override
    {
        m_commandBuffer->uploadBufferData(dst, offset, size, data);
    }

    virtual SLANG_NO_THROW void SLANG_MCALL clearBuffer(IBuffer* buffer, const BufferRange* range = nullptr) override
    {
        SLANG_UNUSED(buffer);
        SLANG_UNUSED(range);
        SLANG_RHI_UNIMPLEMENTED("clearBuffer");
    }

    virtual SLANG_NO_THROW void SLANG_MCALL clearTexture(
        ITexture* texture,
        const ClearValue& clearValue = ClearValue(),
        const SubresourceRange* subresourceRange = nullptr,
        bool clearDepth = true,
        bool clearStencil = true
    ) override
    {
        SLANG_UNUSED(texture);
        SLANG_UNUSED(clearValue);
        SLANG_UNUSED(subresourceRange);
        SLANG_UNUSED(clearDepth);
        SLANG_UNUSED(clearStencil);
        SLANG_RHI_UNIMPLEMENTED("clearTexture");
    }

    virtual SLANG_NO_THROW void SLANG_MCALL
    resolveQuery(IQueryPool* queryPool, GfxIndex index, GfxCount count, IBuffer* buffer, Offset offset) override
    {
        SLANG_UNUSED(queryPool);
        SLANG_UNUSED(index);
        SLANG_UNUSED(count);
        SLANG_UNUSED(buffer);
        SLANG_UNUSED(offset);
        SLANG_RHI_UNIMPLEMENTED("resolveQuery");
    }

    virtual SLANG_NO_THROW void SLANG_MCALL beginRenderPass(const RenderPassDesc& desc) override
    {
        m_commandBuffer->beginRenderPass(desc);
    }

    virtual SLANG_NO_THROW void SLANG_MCALL endRenderPass() override { m_commandBuffer->endRenderPass(); }

    virtual SLANG_NO_THROW void SLANG_MCALL setRenderState(const RenderState& state) override
    {
        m_commandBuffer->setRenderState(state);
    }

    virtual SLANG_NO_THROW void SLANG_MCALL draw(const DrawArguments& args) override { m_commandBuffer->draw(args); }

    virtual SLANG_NO_THROW void SLANG_MCALL drawIndexed(const DrawArguments& args) override
    {
        m_commandBuffer->drawIndexed(args);
    }

    virtual SLANG_NO_THROW void SLANG_MCALL drawIndirect(
        GfxCount maxDrawCount,
        IBuffer* argBuffer,
        Offset argOffset,
        IBuffer* countBuffer = nullptr,
        Offset countOffset = 0
    ) override
    {
        SLANG_UNUSED(maxDrawCount);
        SLANG_UNUSED(argBuffer);
        SLANG_UNUSED(argOffset);
        SLANG_UNUSED(countBuffer);
        SLANG_UNUSED(countOffset);
        SLANG_RHI_UNIMPLEMENTED("drawIndirect");
    }

    virtual SLANG_NO_THROW void SLANG_MCALL drawIndexedIndirect(
        GfxCount maxDrawCount,
        IBuffer* argBuffer,
        Offset argOffset,
        IBuffer* countBuffer = nullptr,
        Offset countOffset = 0
    ) override
    {
        SLANG_UNUSED(maxDrawCount);
        SLANG_UNUSED(argBuffer);
        SLANG_UNUSED(argOffset);
        SLANG_UNUSED(countBuffer);
        SLANG_UNUSED(countOffset);
        SLANG_RHI_UNIMPLEMENTED("drawIndexedIndirect");
    }

    virtual SLANG_NO_THROW void SLANG_MCALL drawMeshTasks(int x, int y, int z) override
    {
        SLANG_UNUSED(x);
        SLANG_UNUSED(y);
        SLANG_UNUSED(z);
        SLANG_RHI_UNIMPLEMENTED("drawMeshTasks");
    }

    virtual SLANG_NO_THROW void SLANG_MCALL setComputeState(const ComputeState& state) override
    {
        m_commandBuffer->setComputeState(state);
    }

    virtual SLANG_NO_THROW void SLANG_MCALL dispatchCompute(int x, int y, int z) override
    {
        m_commandBuffer->dispatchCompute(x, y, z);
    }

    virtual SLANG_NO_THROW void SLANG_MCALL dispatchComputeIndirect(IBuffer* argBuffer, Offset offset) override
    {
        SLANG_UNUSED(argBuffer);
        SLANG_UNUSED(offset);
        SLANG_RHI_UNIMPLEMENTED("dispatchComputeIndirect");
    }

    virtual SLANG_NO_THROW void SLANG_MCALL setRayTracingState(const RayTracingState& state) override
    {
        SLANG_UNUSED(state);
        SLANG_RHI_UNIMPLEMENTED("setRayTracingState");
    }

    virtual SLANG_NO_THROW void SLANG_MCALL
    dispatchRays(GfxIndex rayGenShaderIndex, GfxCount width, GfxCount height, GfxCount depth) override
    {
        SLANG_UNUSED(rayGenShaderIndex);
        SLANG_UNUSED(width);
        SLANG_UNUSED(height);
        SLANG_UNUSED(depth);
        SLANG_RHI_UNIMPLEMENTED("dispatchRays");
    }

    virtual SLANG_NO_THROW void SLANG_MCALL buildAccelerationStructure(
        const AccelerationStructureBuildDesc& desc,
        IAccelerationStructure* dst,
        IAccelerationStructure* src,
        BufferWithOffset scratchBuffer,
        GfxCount propertyQueryCount,
        AccelerationStructureQueryDesc* queryDescs
    ) override
    {
        SLANG_UNUSED(desc);
        SLANG_UNUSED(dst);
        SLANG_UNUSED(src);
        SLANG_UNUSED(scratchBuffer);
        SLANG_UNUSED(propertyQueryCount);
        SLANG_UNUSED(queryDescs);
        SLANG_RHI_UNIMPLEMENTED("buildAccelerationStructure");
    }

    virtual SLANG_NO_THROW void SLANG_MCALL copyAccelerationStructure(
        IAccelerationStructure* dst,
        IAccelerationStructure* src,
        AccelerationStructureCopyMode mode
    ) override
    {
        SLANG_UNUSED(dst);
        SLANG_UNUSED(src);
        SLANG_UNUSED(mode);
        SLANG_RHI_UNIMPLEMENTED("copyAccelerationStructure");
    }

    virtual SLANG_NO_THROW void SLANG_MCALL queryAccelerationStructureProperties(
        GfxCount accelerationStructureCount,
        IAccelerationStructure* const* accelerationStructures,
        GfxCount queryCount,
        AccelerationStructureQueryDesc* queryDescs
    ) override
    {
        SLANG_UNUSED(accelerationStructureCount);
        SLANG_UNUSED(accelerationStructures);
        SLANG_UNUSED(queryCount);
        SLANG_UNUSED(queryDescs);
        SLANG_RHI_UNIMPLEMENTED("queryAccelerationStructureProperties");
    }

    virtual SLANG_NO_THROW void SLANG_MCALL
    serializeAccelerationStructure(BufferWithOffset dst, IAccelerationStructure* src) override
    {
        SLANG_UNUSED(dst);
        SLANG_UNUSED(src);
        SLANG_RHI_UNIMPLEMENTED("serializeAccelerationStructure");
    }

    virtual SLANG_NO_THROW void SLANG_MCALL
    deserializeAccelerationStructure(IAccelerationStructure* dst, BufferWithOffset src) override
    {
        SLANG_UNUSED(dst);
        SLANG_UNUSED(src);
        SLANG_RHI_UNIMPLEMENTED("deserializeAccelerationStructure");
    }

    virtual SLANG_NO_THROW void SLANG_MCALL setBufferState(IBuffer* buffer, ResourceState state) override
    {
        SLANG_UNUSED(buffer);
        SLANG_UNUSED(state);
    }

    virtual SLANG_NO_THROW void SLANG_MCALL
    setTextureState(ITexture* texture, SubresourceRange subresourceRange, ResourceState state) override
    {
        SLANG_UNUSED(texture);
        SLANG_UNUSED(subresourceRange);
        SLANG_UNUSED(state);
    }

    virtual SLANG_NO_THROW void SLANG_MCALL beginDebugEvent(const char* name, float rgbColor[3]) override
    {
        SLANG_UNUSED(name);
        SLANG_UNUSED(rgbColor);
    }

    virtual SLANG_NO_THROW void SLANG_MCALL endDebugEvent() override {}

    virtual SLANG_NO_THROW void SLANG_MCALL writeTimestamp(IQueryPool* pool, GfxIndex index) override
    {
        m_commandBuffer->writeTimestamp(pool, index);
    }

    virtual SLANG_NO_THROW Result SLANG_MCALL finish(ICommandBuffer** outCommandBuffer) override
    {
        if (!m_commandBuffer)
        {
            return SLANG_FAIL;
        }
        returnComPtr(outCommandBuffer, m_commandBuffer);
        m_commandBuffer = nullptr;
        return SLANG_OK;
    }

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override
    {
        *outHandle = {};
        return SLANG_E_NOT_AVAILABLE;
    }
};

class CommandQueueImpl : public ImmediateCommandQueueBase
{
public:
    CommandQueueImpl(ImmediateDevice* device, QueueType type)
        : ImmediateCommandQueueBase(device, type)
    {
    }

    ~CommandQueueImpl() {}

    virtual SLANG_NO_THROW Result SLANG_MCALL createCommandEncoder(ITransientResourceHeap* transientHeap, ICommandEncoder** outEncoder) override
    {
        RefPtr<CommandEncoderImpl> result = new CommandEncoderImpl();
        result->init(m_device, nullptr);
        returnComPtr(outEncoder, result);
        return SLANG_OK;
    }

    virtual SLANG_NO_THROW void SLANG_MCALL
    submit(GfxCount count, ICommandBuffer* const* commandBuffers, IFence* fence, uint64_t valueToSignal) override
    {
        // TODO: implement fence signal.
        SLANG_RHI_ASSERT(fence == nullptr);

        CommandBufferInfo info = {};
        for (GfxIndex i = 0; i < count; i++)
        {
            info.hasWriteTimestamps |= checked_cast<CommandBufferImpl*>(commandBuffers[i])->m_hasWriteTimestamps;
        }
        m_device->beginCommandBuffer(info);
        for (GfxIndex i = 0; i < count; i++)
        {
            checked_cast<CommandBufferImpl*>(commandBuffers[i])->execute(m_device);
        }
        m_device->endCommandBuffer(info);
    }

    virtual SLANG_NO_THROW void SLANG_MCALL waitOnHost() override { m_device->waitForGpu(); }

    virtual SLANG_NO_THROW Result SLANG_MCALL
    waitForFenceValuesOnDevice(GfxCount fenceCount, IFence** fences, uint64_t* waitValues) override
    {
        return SLANG_FAIL;
    }

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override
    {
        return m_device->m_queue->getNativeHandle(outHandle);
    }
};

using TransientResourceHeapImpl = SimpleTransientResourceHeap<ImmediateDevice, CommandEncoderImpl>;

} // namespace

ImmediateDevice::ImmediateDevice()
{
    m_queue = new CommandQueueImpl(this, QueueType::Graphics);
}

Result ImmediateDevice::createTransientResourceHeap(
    const ITransientResourceHeap::Desc& desc,
    ITransientResourceHeap** outHeap
)
{
    RefPtr<TransientResourceHeapImpl> result = new TransientResourceHeapImpl();
    SLANG_RETURN_ON_FAIL(result->init(this, desc));
    returnComPtr(outHeap, result);
    return SLANG_OK;
}

Result ImmediateDevice::getQueue(QueueType type, ICommandQueue** outQueue)
{
    if (type != QueueType::Graphics)
        return SLANG_FAIL;
    m_queue->establishStrongReferenceToDevice();
    returnComPtr(outQueue, m_queue);
    return SLANG_OK;
}

void ImmediateDevice::uploadBufferData(IBuffer* dst, size_t offset, size_t size, void* data)
{
    auto buffer = map(dst, MapFlavor::WriteDiscard);
    memcpy((uint8_t*)buffer + offset, data, size);
    unmap(dst, offset, size);
}

Result ImmediateDevice::readBuffer(IBuffer* buffer, size_t offset, size_t size, ISlangBlob** outBlob)
{
    auto blob = OwnedBlob::create(size);
    auto content = (uint8_t*)map(buffer, MapFlavor::HostRead);
    if (!content)
        return SLANG_FAIL;
    memcpy((void*)blob->getBufferPointer(), content + offset, size);
    unmap(buffer, offset, size);

    returnComPtr(outBlob, blob);
    return SLANG_OK;
}

} // namespace rhi
