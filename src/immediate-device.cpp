#include "immediate-device.h"
#include "command-encoder-com-forward.h"
#include "command-writer.h"
#include "simple-transient-resource-heap.h"

#include "core/common.h"
#include "core/short_vector.h"

namespace rhi {

namespace {

class CommandBufferImpl : public ICommandBuffer, public ComObject
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL
    ICommandBuffer* getInterface(const Guid& guid)
    {
        if (guid == GUID::IID_ISlangUnknown || guid == GUID::IID_ICommandBuffer)
            return static_cast<ICommandBuffer*>(this);
        return nullptr;
    }

public:
    CommandWriter m_writer;
    bool m_hasWriteTimestamps = false;
    RefPtr<ImmediateDevice> m_device;
    RefPtr<ShaderObjectBase> m_rootShaderObject;
    TransientResourceHeap* m_transientHeap;

    void init(ImmediateDevice* device, TransientResourceHeap* transientHeap)
    {
        m_device = device;
        m_transientHeap = transientHeap;
    }

    void reset() { m_writer.clear(); }

    class CommandEncoderImpl : public ICommandEncoder
    {
    public:
        CommandWriter* m_writer;
        CommandBufferImpl* m_commandBuffer;
        void init(CommandBufferImpl* cmdBuffer)
        {
            m_writer = &cmdBuffer->m_writer;
            m_commandBuffer = cmdBuffer;
        }

        virtual void* getInterface(SlangUUID const& uuid)
        {
            if (uuid == GUID::IID_ICommandEncoder || uuid == ISlangUnknown::getTypeGuid())
            {
                return this;
            }
            return nullptr;
        }

        virtual SLANG_NO_THROW Result SLANG_MCALL queryInterface(SlangUUID const& uuid, void** outObject) override
        {
            if (auto ptr = getInterface(uuid))
            {
                *outObject = ptr;
                return SLANG_OK;
            }
            return SLANG_E_NO_INTERFACE;
        }

        virtual SLANG_NO_THROW uint32_t SLANG_MCALL addRef() override { return 1; }
        virtual SLANG_NO_THROW uint32_t SLANG_MCALL release() override { return 1; }

        virtual SLANG_NO_THROW void SLANG_MCALL
        textureBarrier(GfxCount count, ITexture* const* textures, ResourceState src, ResourceState dst) override
        {
            SLANG_UNUSED(count);
            SLANG_UNUSED(textures);
            SLANG_UNUSED(src);
            SLANG_UNUSED(dst);
        }

        virtual SLANG_NO_THROW void SLANG_MCALL textureSubresourceBarrier(
            ITexture* texture,
            SubresourceRange subresourceRange,
            ResourceState src,
            ResourceState dst
        ) override
        {
            SLANG_UNUSED(texture);
            SLANG_UNUSED(subresourceRange);
            SLANG_UNUSED(src);
            SLANG_UNUSED(dst);
        }

        virtual SLANG_NO_THROW void SLANG_MCALL
        bufferBarrier(GfxCount count, IBuffer* const* buffers, ResourceState src, ResourceState dst) override
        {
            SLANG_UNUSED(count);
            SLANG_UNUSED(buffers);
            SLANG_UNUSED(src);
            SLANG_UNUSED(dst);
        }

        virtual SLANG_NO_THROW void SLANG_MCALL beginDebugEvent(const char* name, float rgbColor[3]) override
        {
            SLANG_UNUSED(name);
            SLANG_UNUSED(rgbColor);
        }

        virtual SLANG_NO_THROW void SLANG_MCALL endDebugEvent() override {}

        virtual SLANG_NO_THROW void SLANG_MCALL writeTimestamp(IQueryPool* pool, GfxIndex index) override
        {
            m_writer->writeTimestamp(pool, index);
        }
    };

    class ResourceCommandEncoderImpl : public IResourceCommandEncoder, public CommandEncoderImpl
    {
    public:
        SLANG_RHI_FORWARD_COMMAND_ENCODER_IMPL(CommandEncoderImpl)
        virtual void* getInterface(SlangUUID const& uuid) override
        {
            if (uuid == GUID::IID_IResourceCommandEncoder || uuid == GUID::IID_ICommandEncoder ||
                uuid == ISlangUnknown::getTypeGuid())
            {
                return this;
            }
            return nullptr;
        }

    public:
        virtual SLANG_NO_THROW void SLANG_MCALL endEncoding() override {}

        virtual SLANG_NO_THROW void SLANG_MCALL
        copyBuffer(IBuffer* dst, size_t dstOffset, IBuffer* src, size_t srcOffset, size_t size) override
        {
            m_writer->copyBuffer(dst, dstOffset, src, srcOffset, size);
        }

        virtual SLANG_NO_THROW void SLANG_MCALL
        uploadBufferData(IBuffer* dst, size_t offset, size_t size, void* data) override
        {
            m_writer->uploadBufferData(dst, offset, size, data);
        }

        virtual SLANG_NO_THROW void SLANG_MCALL copyTexture(
            ITexture* dst,
            ResourceState dstState,
            SubresourceRange dstSubresource,
            Offset3D dstOffset,
            ITexture* src,
            ResourceState srcState,
            SubresourceRange srcSubresource,
            Offset3D srcOffset,
            Extents extent
        ) override
        {
            SLANG_UNUSED(dst);
            SLANG_UNUSED(dstState);
            SLANG_UNUSED(dstSubresource);
            SLANG_UNUSED(dstOffset);
            SLANG_UNUSED(src);
            SLANG_UNUSED(srcState);
            SLANG_UNUSED(srcSubresource);
            SLANG_UNUSED(srcOffset);
            SLANG_UNUSED(extent);
            SLANG_RHI_UNIMPLEMENTED("copyTexture");
        }

        virtual SLANG_NO_THROW void SLANG_MCALL uploadTextureData(
            ITexture* dst,
            SubresourceRange subResourceRange,
            Offset3D offset,
            Extents extend,
            SubresourceData* subResourceData,
            GfxCount subResourceDataCount
        ) override
        {
            SLANG_UNUSED(dst);
            SLANG_UNUSED(subResourceRange);
            SLANG_UNUSED(offset);
            SLANG_UNUSED(extend);
            SLANG_UNUSED(subResourceData);
            SLANG_UNUSED(subResourceDataCount);
            SLANG_RHI_UNIMPLEMENTED("uploadTextureData");
        }

        virtual SLANG_NO_THROW void SLANG_MCALL clearBuffer(IBuffer* buffer, const BufferRange* range) override
        {
            SLANG_UNUSED(buffer);
            SLANG_UNUSED(range);
            SLANG_RHI_UNIMPLEMENTED("clearBuffer");
        }

        virtual SLANG_NO_THROW void SLANG_MCALL clearTexture(
            ITexture* texture,
            const ClearValue& clearValue,
            const SubresourceRange* subresourceRange,
            bool clearDepth,
            bool clearStencil
        ) override
        {
            SLANG_UNUSED(texture);
            SLANG_UNUSED(clearValue);
            SLANG_UNUSED(subresourceRange);
            SLANG_UNUSED(clearDepth);
            SLANG_UNUSED(clearStencil);
            SLANG_RHI_UNIMPLEMENTED("clearTexture");
        }

        virtual SLANG_NO_THROW void SLANG_MCALL resolveResource(
            ITexture* source,
            ResourceState sourceState,
            SubresourceRange sourceRange,
            ITexture* dest,
            ResourceState destState,
            SubresourceRange destRange
        ) override
        {
            SLANG_UNUSED(source);
            SLANG_UNUSED(sourceState);
            SLANG_UNUSED(sourceRange);
            SLANG_UNUSED(dest);
            SLANG_UNUSED(destState);
            SLANG_UNUSED(destRange);
            SLANG_RHI_UNIMPLEMENTED("resolveResource");
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

        virtual SLANG_NO_THROW void SLANG_MCALL copyTextureToBuffer(
            IBuffer* dst,
            Offset dstOffset,
            Size dstSize,
            Size dstRowStride,
            ITexture* src,
            ResourceState srcState,
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
            SLANG_UNUSED(srcState);
            SLANG_UNUSED(srcSubresource);
            SLANG_UNUSED(srcOffset);
            SLANG_UNUSED(extent);
            SLANG_RHI_UNIMPLEMENTED("copyTextureToBuffer");
        }
    };

    ResourceCommandEncoderImpl m_resourceCommandEncoder;

    virtual SLANG_NO_THROW Result SLANG_MCALL encodeResourceCommands(IResourceCommandEncoder** outEncoder) override
    {
        m_resourceCommandEncoder.init(this);
        *outEncoder = &m_resourceCommandEncoder;
        return SLANG_OK;
    }

    class RenderCommandEncoderImpl : public IRenderCommandEncoder, public CommandEncoderImpl
    {
    public:
        SLANG_RHI_FORWARD_COMMAND_ENCODER_IMPL(CommandEncoderImpl)
        virtual void* getInterface(SlangUUID const& uuid) override
        {
            if (uuid == GUID::IID_IRenderCommandEncoder || uuid == GUID::IID_ICommandEncoder ||
                uuid == ISlangUnknown::getTypeGuid())
            {
                return this;
            }
            return nullptr;
        }

    public:
        void init(CommandBufferImpl* cmdBuffer, const RenderPassDesc& desc)
        {
            CommandEncoderImpl::init(cmdBuffer);
            m_writer->beginRenderPass(desc);
        }

        virtual SLANG_NO_THROW void SLANG_MCALL endEncoding() override { m_writer->endRenderPass(); }

        virtual SLANG_NO_THROW Result SLANG_MCALL bindPipeline(IPipeline* state, IShaderObject** outRootObject) override
        {
            m_writer->setPipeline(state);
            auto stateImpl = static_cast<Pipeline*>(state);
            SLANG_RETURN_ON_FAIL(m_commandBuffer->m_device->createRootShaderObject(
                stateImpl->m_program,
                m_commandBuffer->m_rootShaderObject.writeRef()
            ));
            *outRootObject = m_commandBuffer->m_rootShaderObject.Ptr();
            return SLANG_OK;
        }

        virtual SLANG_NO_THROW Result SLANG_MCALL
        bindPipelineWithRootObject(IPipeline* state, IShaderObject* rootObject) override
        {
            m_writer->setPipeline(state);
            auto stateImpl = static_cast<Pipeline*>(state);
            SLANG_RETURN_ON_FAIL(m_commandBuffer->m_device->createRootShaderObject(
                stateImpl->m_program,
                m_commandBuffer->m_rootShaderObject.writeRef()
            ));
            m_commandBuffer->m_rootShaderObject->copyFrom(rootObject, m_commandBuffer->m_transientHeap);
            return SLANG_OK;
        }

        virtual SLANG_NO_THROW void SLANG_MCALL setViewports(GfxCount count, const Viewport* viewports) override
        {
            m_writer->setViewports(count, viewports);
        }
        virtual SLANG_NO_THROW void SLANG_MCALL setScissorRects(GfxCount count, const ScissorRect* scissors) override
        {
            m_writer->setScissorRects(count, scissors);
        }
        virtual SLANG_NO_THROW void SLANG_MCALL
        setVertexBuffers(GfxIndex startSlot, GfxCount slotCount, IBuffer* const* buffers, const Offset* offsets)
            override
        {
            m_writer->setVertexBuffers(startSlot, slotCount, buffers, offsets);
        }

        virtual SLANG_NO_THROW void SLANG_MCALL
        setIndexBuffer(IBuffer* buffer, IndexFormat indexFormat, Offset offset) override
        {
            m_writer->setIndexBuffer(buffer, indexFormat, offset);
        }

        virtual SLANG_NO_THROW Result SLANG_MCALL draw(GfxCount vertexCount, GfxIndex startVertex) override
        {
            m_writer->bindRootShaderObject(m_commandBuffer->m_rootShaderObject);
            m_writer->draw(vertexCount, startVertex);
            return SLANG_OK;
        }

        virtual SLANG_NO_THROW Result SLANG_MCALL
        drawIndexed(GfxCount indexCount, GfxIndex startIndex, GfxIndex baseVertex) override
        {
            m_writer->bindRootShaderObject(m_commandBuffer->m_rootShaderObject);
            m_writer->drawIndexed(indexCount, startIndex, baseVertex);
            return SLANG_OK;
        }

        virtual SLANG_NO_THROW void SLANG_MCALL setStencilReference(uint32_t referenceValue) override
        {
            m_writer->setStencilReference(referenceValue);
        }

        virtual SLANG_NO_THROW Result SLANG_MCALL drawIndirect(
            GfxCount maxDrawCount,
            IBuffer* argBuffer,
            Offset argOffset,
            IBuffer* countBuffer,
            Offset countOffset
        ) override
        {
            SLANG_UNUSED(maxDrawCount);
            SLANG_UNUSED(argBuffer);
            SLANG_UNUSED(argOffset);
            SLANG_UNUSED(countBuffer);
            SLANG_UNUSED(countOffset);
            SLANG_RHI_UNIMPLEMENTED("ImmediateRenderBase::drawIndirect");
            return SLANG_OK;
        }

        virtual SLANG_NO_THROW Result SLANG_MCALL drawIndexedIndirect(
            GfxCount maxDrawCount,
            IBuffer* argBuffer,
            Offset argOffset,
            IBuffer* countBuffer,
            Offset countOffset
        ) override
        {
            SLANG_UNUSED(maxDrawCount);
            SLANG_UNUSED(argBuffer);
            SLANG_UNUSED(argOffset);
            SLANG_UNUSED(countBuffer);
            SLANG_UNUSED(countOffset);
            SLANG_RHI_UNIMPLEMENTED("ImmediateRenderBase::drawIndirect");
            return SLANG_OK;
        }

        virtual SLANG_NO_THROW Result SLANG_MCALL drawMeshTasks(int, int, int) override
        {
            SLANG_RHI_UNIMPLEMENTED("ImmediateRenderBase::drawMeshTasks");
        }

        virtual SLANG_NO_THROW Result SLANG_MCALL setSamplePositions(
            GfxCount samplesPerPixel,
            GfxCount pixelCount,
            const SamplePosition* samplePositions
        ) override
        {
            SLANG_UNUSED(samplesPerPixel);
            SLANG_UNUSED(pixelCount);
            SLANG_UNUSED(samplePositions);
            return SLANG_E_NOT_AVAILABLE;
        }

        virtual SLANG_NO_THROW Result SLANG_MCALL drawInstanced(
            GfxCount vertexCount,
            GfxCount instanceCount,
            GfxIndex startVertex,
            GfxIndex startInstanceLocation
        ) override
        {
            m_writer->bindRootShaderObject(m_commandBuffer->m_rootShaderObject);
            m_writer->drawInstanced(vertexCount, instanceCount, startVertex, startInstanceLocation);
            return SLANG_OK;
        }

        virtual SLANG_NO_THROW Result SLANG_MCALL drawIndexedInstanced(
            GfxCount indexCount,
            GfxCount instanceCount,
            GfxIndex startIndexLocation,
            GfxIndex baseVertexLocation,
            GfxIndex startInstanceLocation
        ) override
        {
            m_writer->bindRootShaderObject(m_commandBuffer->m_rootShaderObject);
            m_writer->drawIndexedInstanced(
                indexCount,
                instanceCount,
                startIndexLocation,
                baseVertexLocation,
                startInstanceLocation
            );
            return SLANG_OK;
        }
    };

    RenderCommandEncoderImpl m_renderCommandEncoder;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    encodeRenderCommands(const RenderPassDesc& desc, IRenderCommandEncoder** outEncoder) override
    {
        m_renderCommandEncoder.init(this, desc);
        *outEncoder = &m_renderCommandEncoder;
        return SLANG_OK;
    }

    class ComputeCommandEncoderImpl : public IComputeCommandEncoder, public CommandEncoderImpl
    {
    public:
        SLANG_RHI_FORWARD_COMMAND_ENCODER_IMPL(CommandEncoderImpl)
        virtual void* getInterface(SlangUUID const& uuid) override
        {
            if (uuid == GUID::IID_IComputeCommandEncoder || uuid == GUID::IID_ICommandEncoder ||
                uuid == ISlangUnknown::getTypeGuid())
            {
                return this;
            }
            return nullptr;
        }

    public:
        virtual SLANG_NO_THROW void SLANG_MCALL endEncoding() override {}

        virtual SLANG_NO_THROW Result SLANG_MCALL bindPipeline(IPipeline* state, IShaderObject** outRootObject) override
        {
            m_writer->setPipeline(state);
            auto stateImpl = static_cast<Pipeline*>(state);
            SLANG_RETURN_ON_FAIL(m_commandBuffer->m_device->createRootShaderObject(
                stateImpl->m_program,
                m_commandBuffer->m_rootShaderObject.writeRef()
            ));
            *outRootObject = m_commandBuffer->m_rootShaderObject.Ptr();
            return SLANG_OK;
        }

        virtual SLANG_NO_THROW Result SLANG_MCALL
        bindPipelineWithRootObject(IPipeline* state, IShaderObject* rootObject) override
        {
            m_writer->setPipeline(state);
            auto stateImpl = static_cast<Pipeline*>(state);
            SLANG_RETURN_ON_FAIL(m_commandBuffer->m_device->createRootShaderObject(
                stateImpl->m_program,
                m_commandBuffer->m_rootShaderObject.writeRef()
            ));
            m_commandBuffer->m_rootShaderObject->copyFrom(rootObject, m_commandBuffer->m_transientHeap);
            return SLANG_OK;
        }

        virtual SLANG_NO_THROW Result SLANG_MCALL dispatchCompute(int x, int y, int z) override
        {
            m_writer->bindRootShaderObject(m_commandBuffer->m_rootShaderObject);
            m_writer->dispatchCompute(x, y, z);
            return SLANG_OK;
        }

        virtual SLANG_NO_THROW Result SLANG_MCALL dispatchComputeIndirect(IBuffer* argBuffer, Offset offset) override
        {
            SLANG_RHI_UNIMPLEMENTED("ImmediateRenderBase::dispatchComputeIndirect");
        }
    };

    ComputeCommandEncoderImpl m_computeCommandEncoder;
    virtual SLANG_NO_THROW Result SLANG_MCALL encodeComputeCommands(IComputeCommandEncoder** outEncoder) override
    {
        m_computeCommandEncoder.init(this);
        *outEncoder = &m_computeCommandEncoder;
        return SLANG_OK;
    }

    virtual SLANG_NO_THROW Result SLANG_MCALL encodeRayTracingCommands(IRayTracingCommandEncoder** outEncoder) override
    {
        *outEncoder = nullptr;
        return SLANG_OK;
    }

    virtual SLANG_NO_THROW void SLANG_MCALL close() override {}

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override
    {
        *outHandle = {};
        return SLANG_E_NOT_AVAILABLE;
    }

    void execute()
    {
        for (auto& cmd : m_writer.m_commands)
        {
            auto name = cmd.name;
            switch (name)
            {
            case CommandName::SetPipeline:
                m_device->setPipeline(m_writer.getObject<Pipeline>(cmd.operands[0]));
                break;
            case CommandName::BindRootShaderObject:
                m_device->bindRootShaderObject(m_writer.getObject<ShaderObjectBase>(cmd.operands[0]));
                break;
            case CommandName::BeginRenderPass:
            {
                RenderPassDesc desc;
                if (cmd.operands[0] > 0)
                {
                    desc.colorAttachments = m_writer.getData<RenderPassColorAttachment>(cmd.operands[2]);
                    desc.colorAttachmentCount = cmd.operands[0];
                }
                if (cmd.operands[1] > 0)
                {
                    desc.depthStencilAttachment = m_writer.getData<RenderPassDepthStencilAttachment>(cmd.operands[3]);
                }
                m_device->beginRenderPass(desc);
                break;
            }
            case CommandName::EndRenderPass:
                m_device->endRenderPass();
                break;
            case CommandName::SetViewports:
                m_device->setViewports((UInt)cmd.operands[0], m_writer.getData<Viewport>(cmd.operands[1]));
                break;
            case CommandName::SetScissorRects:
                m_device->setScissorRects((UInt)cmd.operands[0], m_writer.getData<ScissorRect>(cmd.operands[1]));
                break;
            case CommandName::SetVertexBuffers:
            {
                short_vector<IBuffer*> buffers;
                for (uint32_t i = 0; i < cmd.operands[1]; i++)
                {
                    buffers.push_back(m_writer.getObject<Buffer>(cmd.operands[2] + i));
                }
                m_device->setVertexBuffers(
                    cmd.operands[0],
                    cmd.operands[1],
                    buffers.data(),
                    m_writer.getData<Offset>(cmd.operands[3])
                );
            }
            break;
            case CommandName::SetIndexBuffer:
                m_device->setIndexBuffer(
                    m_writer.getObject<Buffer>(cmd.operands[0]),
                    (IndexFormat)cmd.operands[1],
                    (UInt)cmd.operands[2]
                );
                break;
            case CommandName::Draw:
                m_device->draw(cmd.operands[0], cmd.operands[1]);
                break;
            case CommandName::DrawIndexed:
                m_device->drawIndexed(cmd.operands[0], cmd.operands[1], cmd.operands[2]);
                break;
            case CommandName::DrawInstanced:
                m_device->drawInstanced(cmd.operands[0], cmd.operands[1], cmd.operands[2], cmd.operands[3]);
                break;
            case CommandName::DrawIndexedInstanced:
                m_device->drawIndexedInstanced(
                    cmd.operands[0],
                    cmd.operands[1],
                    cmd.operands[2],
                    cmd.operands[3],
                    cmd.operands[4]
                );
                break;
            case CommandName::SetStencilReference:
                m_device->setStencilReference(cmd.operands[0]);
                break;
            case CommandName::DispatchCompute:
                m_device->dispatchCompute(int(cmd.operands[0]), int(cmd.operands[1]), int(cmd.operands[2]));
                break;
            case CommandName::UploadBufferData:
                m_device->uploadBufferData(
                    m_writer.getObject<Buffer>(cmd.operands[0]),
                    cmd.operands[1],
                    cmd.operands[2],
                    m_writer.getData<uint8_t>(cmd.operands[3])
                );
                break;
            case CommandName::CopyBuffer:
                m_device->copyBuffer(
                    m_writer.getObject<Buffer>(cmd.operands[0]),
                    cmd.operands[1],
                    m_writer.getObject<Buffer>(cmd.operands[2]),
                    cmd.operands[3],
                    cmd.operands[4]
                );
                break;
            case CommandName::WriteTimestamp:
                m_device->writeTimestamp(m_writer.getObject<QueryPool>(cmd.operands[0]), (GfxIndex)cmd.operands[1]);
                break;
            default:
                SLANG_RHI_ASSERT_FAILURE("Unknown command");
                break;
            }
        }
        m_writer.clear();
    }
};

class CommandQueueImpl : public ImmediateCommandQueueBase
{
public:
    ICommandQueue::Desc m_desc;

    ImmediateDevice* getDevice() { return static_cast<ImmediateDevice*>(m_device.get()); }

    CommandQueueImpl(ImmediateDevice* device)
    {
        // Don't establish strong reference to `Device` at start, because
        // there will be only one instance of command queue and it will be
        // owned by `Device`. We should establish a strong reference only
        // when there are external references to the command queue.
        m_device.setWeakReference(device);
        m_desc.type = ICommandQueue::QueueType::Graphics;
    }

    ~CommandQueueImpl() { getDevice()->m_queueCreateCount--; }

    virtual SLANG_NO_THROW const Desc& SLANG_MCALL getDesc() override { return m_desc; }

    virtual SLANG_NO_THROW void SLANG_MCALL
    executeCommandBuffers(GfxCount count, ICommandBuffer* const* commandBuffers, IFence* fence, uint64_t valueToSignal)
        override
    {
        // TODO: implement fence signal.
        SLANG_RHI_ASSERT(fence == nullptr);

        CommandBufferInfo info = {};
        for (GfxIndex i = 0; i < count; i++)
        {
            info.hasWriteTimestamps |=
                static_cast<CommandBufferImpl*>(commandBuffers[i])->m_writer.m_hasWriteTimestamps;
        }
        static_cast<ImmediateDevice*>(m_device.get())->beginCommandBuffer(info);
        for (GfxIndex i = 0; i < count; i++)
        {
            static_cast<CommandBufferImpl*>(commandBuffers[i])->execute();
        }
        static_cast<ImmediateDevice*>(m_device.get())->endCommandBuffer(info);
    }

    virtual SLANG_NO_THROW void SLANG_MCALL waitOnHost() override { getDevice()->waitForGpu(); }

    virtual SLANG_NO_THROW Result SLANG_MCALL
    waitForFenceValuesOnDevice(GfxCount fenceCount, IFence** fences, uint64_t* waitValues) override
    {
        return SLANG_FAIL;
    }

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override
    {
        return getDevice()->m_queue->getNativeHandle(outHandle);
    }
};

using TransientResourceHeapImpl = SimpleTransientResourceHeap<ImmediateDevice, CommandBufferImpl>;

} // namespace

ImmediateDevice::ImmediateDevice()
{
    m_queue = new CommandQueueImpl(this);
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

Result ImmediateDevice::createCommandQueue(const ICommandQueue::Desc& desc, ICommandQueue** outQueue)
{
    SLANG_UNUSED(desc);
    // Only one queue is supported.
    if (m_queueCreateCount != 0)
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
