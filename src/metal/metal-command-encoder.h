#pragma once

#include "metal-base.h"
#include "metal-pipeline.h"

#include "core/short_vector.h"

namespace rhi::metal {

class PassEncoderImpl : public IPassEncoder
{
public:
    virtual void* getInterface(SlangUUID const& uuid)
    {
        if (uuid == GUID::IID_IPassEncoder || uuid == ISlangUnknown::getTypeGuid())
            return this;
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

public:
    virtual SLANG_NO_THROW void SLANG_MCALL setBufferState(IBuffer* buffer, ResourceState state) override;
    virtual SLANG_NO_THROW void SLANG_MCALL
    setTextureState(ITexture* texture, SubresourceRange subresourceRange, ResourceState state) override;

    virtual SLANG_NO_THROW void SLANG_MCALL beginDebugEvent(const char* name, float rgbColor[3]) override;
    virtual SLANG_NO_THROW void SLANG_MCALL endDebugEvent() override;

    virtual SLANG_NO_THROW void SLANG_MCALL writeTimestamp(IQueryPool* pool, GfxIndex index) override;

public:
    CommandBufferImpl* m_commandBuffer;
    MTL::CommandBuffer* m_metalCommandBuffer;
    RefPtr<Pipeline> m_currentPipeline;

    void init(CommandBufferImpl* commandBuffer);
    void endEncodingImpl();

    Result setPipelineImpl(IPipeline* state, IShaderObject** outRootObject);
};

class ResourcePassEncoderImpl : public IResourcePassEncoder, public PassEncoderImpl
{
public:
    SLANG_RHI_FORWARD_PASS_ENCODER_IMPL(PassEncoderImpl)
    virtual void* getInterface(SlangUUID const& uuid) override
    {
        if (uuid == GUID::IID_IResourcePassEncoder || uuid == GUID::IID_IPassEncoder ||
            uuid == ISlangUnknown::getTypeGuid())
        {
            return this;
        }
        return nullptr;
    }

public:
    virtual SLANG_NO_THROW void SLANG_MCALL end() override;

    virtual SLANG_NO_THROW void SLANG_MCALL
    copyBuffer(IBuffer* dst, Offset dstOffset, IBuffer* src, Offset srcOffset, Size size) override;

    virtual SLANG_NO_THROW void SLANG_MCALL copyTexture(
        ITexture* dst,
        SubresourceRange dstSubresource,
        Offset3D dstOffset,
        ITexture* src,
        SubresourceRange srcSubresource,
        Offset3D srcOffset,
        Extents extent
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
    uploadBufferData(IBuffer* buffer, Offset offset, Size size, void* data) override;

    virtual SLANG_NO_THROW void SLANG_MCALL uploadTextureData(
        ITexture* dst,
        SubresourceRange subresourceRange,
        Offset3D offset,
        Extents extend,
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

    virtual SLANG_NO_THROW void SLANG_MCALL
    resolveQuery(IQueryPool* queryPool, GfxIndex index, GfxCount count, IBuffer* buffer, Offset offset) override;
};

class RenderPassEncoderImpl : public IRenderPassEncoder, public PassEncoderImpl
{
public:
    SLANG_RHI_FORWARD_PASS_ENCODER_IMPL(PassEncoderImpl)
    virtual void* getInterface(SlangUUID const& uuid) override
    {
        if (uuid == GUID::IID_IRenderPassEncoder || uuid == GUID::IID_IPassEncoder ||
            uuid == ISlangUnknown::getTypeGuid())
        {
            return this;
        }
        return nullptr;
    }

public:
    NS::SharedPtr<MTL::RenderPassDescriptor> m_renderPassDesc;
    short_vector<RefPtr<TextureViewImpl>> m_renderTargetViews;
    RefPtr<TextureViewImpl> m_depthStencilView;

    short_vector<MTL::Viewport, 16> m_viewports;
    short_vector<MTL::ScissorRect, 16> m_scissorRects;
    MTL::PrimitiveType m_primitiveType = MTL::PrimitiveTypeTriangle;

    short_vector<MTL::Buffer*, 16> m_vertexBuffers;
    short_vector<NS::UInteger, 16> m_vertexBufferOffsets;

    MTL::Buffer* m_indexBuffer = nullptr;
    NS::UInteger m_indexBufferOffset = 0;
    MTL::IndexType m_indexBufferType = MTL::IndexTypeUInt16;

    uint32_t m_stencilReferenceValue = 0;

public:
    Result beginPass(const RenderPassDesc& desc);

    virtual SLANG_NO_THROW void SLANG_MCALL end() override;

    virtual SLANG_NO_THROW Result SLANG_MCALL bindPipeline(IPipeline* pipeline, IShaderObject** outRootObject) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    bindPipelineWithRootObject(IPipeline* pipeline, IShaderObject* rootObject) override;

    virtual SLANG_NO_THROW void SLANG_MCALL setViewports(GfxCount count, const Viewport* viewports) override;

    virtual SLANG_NO_THROW void SLANG_MCALL setScissorRects(GfxCount count, const ScissorRect* rects) override;

    virtual SLANG_NO_THROW void SLANG_MCALL
    setVertexBuffers(GfxIndex startSlot, GfxCount slotCount, IBuffer* const* buffers, const Offset* offsets) override;

    virtual SLANG_NO_THROW void SLANG_MCALL
    setIndexBuffer(IBuffer* buffer, IndexFormat indexFormat, Offset offset = 0) override;

    virtual SLANG_NO_THROW void SLANG_MCALL setStencilReference(uint32_t referenceValue) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    setSamplePositions(GfxCount samplesPerPixel, GfxCount pixelCount, const SamplePosition* samplePositions) override;

    Result prepareDraw(MTL::RenderCommandEncoder*& encoder);

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

class ComputePassEncoderImpl : public IComputePassEncoder, public PassEncoderImpl
{
public:
    SLANG_RHI_FORWARD_PASS_ENCODER_IMPL(PassEncoderImpl)
    virtual void* getInterface(SlangUUID const& uuid) override
    {
        if (uuid == GUID::IID_IComputePassEncoder || uuid == GUID::IID_IPassEncoder ||
            uuid == ISlangUnknown::getTypeGuid())
        {
            return this;
        }
        return nullptr;
    }

public:
    virtual SLANG_NO_THROW void SLANG_MCALL end() override;

    virtual SLANG_NO_THROW Result SLANG_MCALL bindPipeline(IPipeline* pipeline, IShaderObject** outRootObject) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    bindPipelineWithRootObject(IPipeline* pipeline, IShaderObject* rootObject) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL dispatchCompute(int x, int y, int z) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL dispatchComputeIndirect(IBuffer* argBuffer, Offset offset) override;
};

class RayTracingPassEncoderImpl : public IRayTracingPassEncoder, public PassEncoderImpl
{
public:
    SLANG_RHI_FORWARD_PASS_ENCODER_IMPL(PassEncoderImpl)
    virtual void* getInterface(SlangUUID const& uuid) override
    {
        if (uuid == GUID::IID_IRayTracingPassEncoder || uuid == GUID::IID_IPassEncoder ||
            uuid == ISlangUnknown::getTypeGuid())
        {
            return this;
        }
        return nullptr;
    }

public:
    virtual SLANG_NO_THROW void SLANG_MCALL end() override;

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

    virtual SLANG_NO_THROW Result SLANG_MCALL bindPipeline(IPipeline* pipeline, IShaderObject** outRootObject) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    bindPipelineWithRootObject(IPipeline* pipeline, IShaderObject* rootObject) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    dispatchRays(GfxIndex raygenShaderIndex, IShaderTable* shaderTable, GfxCount width, GfxCount height, GfxCount depth)
        override;
};

} // namespace rhi::metal
