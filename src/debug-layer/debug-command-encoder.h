#pragma once

#include "debug-base.h"

namespace rhi::debug {

class DebugCommandEncoder : public ICommandEncoder
{
public:
    DebugCommandBuffer* getCommandBuffer() { return commandBuffer; }
    bool getIsOpen() { return isOpen; }
    virtual ICommandEncoder* getBaseObject() = 0;

    virtual void* getInterface(SlangUUID const& uuid) = 0;
    Result queryInterface(SlangUUID const& uuid, void** outObject) override
    {
        if (auto ptr = getInterface(uuid))
        {
            *outObject = ptr;
            return SLANG_OK;
        }
        return SLANG_E_NO_INTERFACE;
    }
    uint32_t addRef() override { return 2; }
    uint32_t release() override { return 2; }

    virtual SLANG_NO_THROW void SLANG_MCALL setBufferState(IBuffer* buffer, ResourceState state) override;

    virtual SLANG_NO_THROW void SLANG_MCALL
    setTextureState(ITexture* texture, SubresourceRange subresourceRange, ResourceState state) override;

    virtual SLANG_NO_THROW void SLANG_MCALL beginDebugEvent(const char* name, float rgbColor[3]) override;
    virtual SLANG_NO_THROW void SLANG_MCALL endDebugEvent() override;

    virtual SLANG_NO_THROW void SLANG_MCALL writeTimestamp(IQueryPool* queryPool, GfxIndex queryIndex) override;


public:
    DebugCommandBuffer* commandBuffer;
    bool isOpen = false;
};

class DebugResourceCommandEncoder : public UnownedDebugObject<IResourceCommandEncoder>, public DebugCommandEncoder
{
public:
    SLANG_RHI_FORWARD_COMMAND_ENCODER_IMPL(DebugCommandEncoder)

    virtual ICommandEncoder* getBaseObject() override { return baseObject; }

    virtual void* getInterface(SlangUUID const& uuid) override
    {
        if (uuid == GUID::IID_ICommandEncoder || uuid == GUID::IID_IResourceCommandEncoder ||
            uuid == ISlangUnknown::getTypeGuid())
        {
            return this;
        }
        return nullptr;
    }

public:
    virtual SLANG_NO_THROW void SLANG_MCALL endEncoding() override;

    virtual SLANG_NO_THROW void SLANG_MCALL
    copyBuffer(IBuffer* dst, Offset dstOffset, IBuffer* src, Offset srcOffset, Size size) override;

    virtual SLANG_NO_THROW void SLANG_MCALL
    uploadBufferData(IBuffer* dst, Offset offset, Size size, void* data) override;

    virtual SLANG_NO_THROW void SLANG_MCALL copyTexture(
        ITexture* dst,
        SubresourceRange dstSubresource,
        Offset3D dstOffset,
        ITexture* src,
        SubresourceRange srcSubresource,
        Offset3D srcOffset,
        Extents extent
    ) override;

    virtual SLANG_NO_THROW void SLANG_MCALL uploadTextureData(
        ITexture* dst,
        SubresourceRange subresourceRange,
        Offset3D offset,
        Extents extent,
        SubresourceData* subresourceData,
        GfxCount subresourceDataCount
    ) override;

    virtual SLANG_NO_THROW void SLANG_MCALL clearBuffer(IBuffer* buffer, const BufferRange* range) override;

    virtual SLANG_NO_THROW void SLANG_MCALL clearTexture(
        ITexture* texture,
        const ClearValue& clearValue,
        const SubresourceRange* subresourceRange,
        bool clearDepth,
        bool clearStencil
    ) override;

    virtual SLANG_NO_THROW void SLANG_MCALL copyTextureToBuffer(
        IBuffer* dst,
        Offset dstOffset,
        Size dstSize,
        Size dstRowStride,
        ITexture* src,
        SubresourceRange srcSubresource,
        Offset3D srcOffset,
        Extents extent
    ) override;

    virtual SLANG_NO_THROW void SLANG_MCALL
    resolveQuery(IQueryPool* queryPool, GfxIndex index, GfxCount count, IBuffer* buffer, Offset offset) override;
};

class DebugRenderCommandEncoder : public UnownedDebugObject<IRenderCommandEncoder>, public DebugCommandEncoder
{
public:
    SLANG_RHI_FORWARD_COMMAND_ENCODER_IMPL(DebugCommandEncoder)

    virtual ICommandEncoder* getBaseObject() override { return baseObject; }

    virtual void* getInterface(SlangUUID const& uuid) override
    {
        if (uuid == GUID::IID_ICommandEncoder || uuid == GUID::IID_IRenderCommandEncoder ||
            uuid == ISlangUnknown::getTypeGuid())
        {
            return this;
        }
        return nullptr;
    }

public:
    virtual SLANG_NO_THROW void SLANG_MCALL endEncoding() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    bindPipeline(IPipeline* state, IShaderObject** outRootShaderObject) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    bindPipelineWithRootObject(IPipeline* state, IShaderObject* rootObject) override;
    virtual SLANG_NO_THROW void SLANG_MCALL setViewports(GfxCount count, const Viewport* viewports) override;
    virtual SLANG_NO_THROW void SLANG_MCALL setScissorRects(GfxCount count, const ScissorRect* scissors) override;
    virtual SLANG_NO_THROW void SLANG_MCALL
    setVertexBuffers(GfxIndex startSlot, GfxCount slotCount, IBuffer* const* buffers, const Offset* offsets) override;
    virtual SLANG_NO_THROW void SLANG_MCALL
    setIndexBuffer(IBuffer* buffer, IndexFormat indexFormat, Offset offset = 0) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL draw(GfxCount vertexCount, GfxIndex startVertex = 0) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    drawIndexed(GfxCount indexCount, GfxIndex startIndex = 0, GfxIndex baseVertex = 0) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    drawIndirect(GfxCount maxDrawCount, IBuffer* argBuffer, Offset argOffset, IBuffer* countBuffer, Offset countOffset)
        override;
    virtual SLANG_NO_THROW Result SLANG_MCALL drawIndexedIndirect(
        GfxCount maxDrawCount,
        IBuffer* argBuffer,
        Offset argOffset,
        IBuffer* countBuffer,
        Offset countOffset
    ) override;
    virtual SLANG_NO_THROW void SLANG_MCALL setStencilReference(uint32_t referenceValue) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    setSamplePositions(GfxCount samplesPerPixel, GfxCount pixelCount, const SamplePosition* samplePositions) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    drawInstanced(GfxCount vertexCount, GfxCount instanceCount, GfxIndex startVertex, GfxIndex startInstanceLocation)
        override;

    virtual SLANG_NO_THROW Result SLANG_MCALL drawIndexedInstanced(
        GfxCount indexCount,
        GfxCount instanceCount,
        GfxIndex startIndexLocation,
        GfxIndex baseVertexLocation,
        GfxIndex startInstanceLocation
    ) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL drawMeshTasks(int x, int y, int z) override;
};


class DebugComputeCommandEncoder : public UnownedDebugObject<IComputeCommandEncoder>, public DebugCommandEncoder
{
public:
    SLANG_RHI_FORWARD_COMMAND_ENCODER_IMPL(DebugCommandEncoder)

    virtual ICommandEncoder* getBaseObject() override { return baseObject; }

    virtual void* getInterface(SlangUUID const& uuid) override
    {
        if (uuid == GUID::IID_ICommandEncoder || uuid == GUID::IID_IComputeCommandEncoder ||
            uuid == ISlangUnknown::getTypeGuid())
        {
            return this;
        }
        return nullptr;
    }

public:
    virtual SLANG_NO_THROW void SLANG_MCALL endEncoding() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    bindPipeline(IPipeline* state, IShaderObject** outRootShaderObject) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    bindPipelineWithRootObject(IPipeline* state, IShaderObject* rootObject) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL dispatchCompute(int x, int y, int z) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL dispatchComputeIndirect(IBuffer* cmdBuffer, Offset offset) override;
};

class DebugRayTracingCommandEncoder : public UnownedDebugObject<IRayTracingCommandEncoder>, public DebugCommandEncoder
{
public:
    SLANG_RHI_FORWARD_COMMAND_ENCODER_IMPL(DebugCommandEncoder)

    virtual ICommandEncoder* getBaseObject() override { return baseObject; }

    virtual void* getInterface(SlangUUID const& uuid) override
    {
        if (uuid == GUID::IID_ICommandEncoder || uuid == GUID::IID_IRayTracingCommandEncoder ||
            uuid == ISlangUnknown::getTypeGuid())
        {
            return this;
        }
        return nullptr;
    }

public:
    virtual SLANG_NO_THROW void SLANG_MCALL endEncoding() override;
    virtual SLANG_NO_THROW void SLANG_MCALL buildAccelerationStructure(
        const AccelerationStructureBuildDesc& desc,
        IAccelerationStructure* dst,
        IAccelerationStructure* src,
        BufferWithOffset scratchBuffer,
        GfxCount propertyQueryCount,
        AccelerationStructureQueryDesc* queryDescs
    ) override;
    virtual SLANG_NO_THROW void SLANG_MCALL copyAccelerationStructure(
        IAccelerationStructure* dst,
        IAccelerationStructure* src,
        AccelerationStructureCopyMode mode
    ) override;
    virtual SLANG_NO_THROW void SLANG_MCALL queryAccelerationStructureProperties(
        GfxCount accelerationStructureCount,
        IAccelerationStructure* const* accelerationStructures,
        GfxCount queryCount,
        AccelerationStructureQueryDesc* queryDescs
    ) override;
    virtual SLANG_NO_THROW void SLANG_MCALL
    serializeAccelerationStructure(BufferWithOffset dst, IAccelerationStructure* src) override;
    virtual SLANG_NO_THROW void SLANG_MCALL
    deserializeAccelerationStructure(IAccelerationStructure* dst, BufferWithOffset src) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL bindPipeline(IPipeline* state, IShaderObject** outRootObject) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    bindPipelineWithRootObject(IPipeline* state, IShaderObject* rootObject) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    dispatchRays(GfxIndex rayGenShaderIndex, IShaderTable* shaderTable, GfxCount width, GfxCount height, GfxCount depth)
        override;
};

} // namespace rhi::debug
