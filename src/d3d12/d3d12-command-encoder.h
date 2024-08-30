#pragma once

#include "d3d12-base.h"
#include "d3d12-buffer.h"
#include "d3d12-framebuffer.h"
#include "d3d12-render-pass.h"
#include "d3d12-submitter.h"

namespace rhi::d3d12 {

static const Int kMaxRTVCount = 8;

class PipelineCommandEncoder
{
public:
    bool m_isOpen = false;
    bool m_bindingDirty = true;
    CommandBufferImpl* m_commandBuffer;
    TransientResourceHeapImpl* m_transientHeap;
    DeviceImpl* m_renderer;
    ID3D12Device* m_device;
    ID3D12GraphicsCommandList* m_d3dCmdList;
    ID3D12GraphicsCommandList6* m_d3dCmdList6;
    ID3D12GraphicsCommandList* m_preCmdList = nullptr;

    RefPtr<PipelineStateBase> m_currentPipeline;

    static int getBindPointIndex(PipelineType type);

    void init(CommandBufferImpl* commandBuffer);

    void endEncodingImpl() { m_isOpen = false; }

    Result bindPipelineImpl(IPipelineState* pipelineState, IShaderObject** outRootObject);

    Result bindPipelineWithRootObjectImpl(IPipelineState* pipelineState, IShaderObject* rootObject);

    /// Specializes the pipeline according to current root-object argument values,
    /// applys the root object bindings and binds the pipeline state.
    /// The newly specialized pipeline is held alive by the pipeline cache so users of
    /// `newPipeline` do not need to maintain its lifespan.
    Result _bindRenderState(Submitter* submitter, RefPtr<PipelineStateBase>& newPipeline);
};

class ResourceCommandEncoderImpl : public IResourceCommandEncoder, public PipelineCommandEncoder
{
public:
    virtual void* getInterface(SlangUUID const& uuid)
    {
        if (uuid == GUID::IID_IResourceCommandEncoder || uuid == ISlangUnknown::getTypeGuid())
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

    virtual SLANG_NO_THROW void SLANG_MCALL
    copyBuffer(IBuffer* dst, Offset dstOffset, IBuffer* src, Offset srcOffset, Size size) override;
    virtual SLANG_NO_THROW void SLANG_MCALL
    uploadBufferData(IBuffer* dst, Offset offset, Size size, void* data) override;
    virtual SLANG_NO_THROW void SLANG_MCALL
    textureBarrier(GfxCount count, ITexture* const* textures, ResourceState src, ResourceState dst) override;
    virtual SLANG_NO_THROW void SLANG_MCALL
    bufferBarrier(GfxCount count, IBuffer* const* buffers, ResourceState src, ResourceState dst) override;
    virtual SLANG_NO_THROW void SLANG_MCALL endEncoding() override {}
    virtual SLANG_NO_THROW void SLANG_MCALL writeTimestamp(IQueryPool* pool, GfxIndex index) override;
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
    ) override;

    virtual SLANG_NO_THROW void SLANG_MCALL uploadTextureData(
        ITexture* dst,
        SubresourceRange subResourceRange,
        Offset3D offset,
        Extents extent,
        SubresourceData* subResourceData,
        GfxCount subResourceDataCount
    ) override;

    virtual SLANG_NO_THROW void SLANG_MCALL
    clearResourceView(IResourceView* view, ClearValue* clearValue, ClearResourceViewFlags::Enum flags) override;

    virtual SLANG_NO_THROW void SLANG_MCALL resolveResource(
        ITexture* source,
        ResourceState sourceState,
        SubresourceRange sourceRange,
        ITexture* dest,
        ResourceState destState,
        SubresourceRange destRange
    ) override;

    virtual SLANG_NO_THROW void SLANG_MCALL
    resolveQuery(IQueryPool* queryPool, GfxIndex index, GfxCount count, IBuffer* buffer, Offset offset) override;

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
    ) override;

    virtual SLANG_NO_THROW void SLANG_MCALL textureSubresourceBarrier(
        ITexture* texture,
        SubresourceRange subresourceRange,
        ResourceState src,
        ResourceState dst
    ) override;

    virtual SLANG_NO_THROW void SLANG_MCALL beginDebugEvent(const char* name, float rgbColor[3]) override;
    virtual SLANG_NO_THROW void SLANG_MCALL endDebugEvent() override;
};

class ComputeCommandEncoderImpl : public IComputeCommandEncoder, public ResourceCommandEncoderImpl
{
public:
    SLANG_RHI_FORWARD_RESOURCE_COMMAND_ENCODER_IMPL(ResourceCommandEncoderImpl)
    virtual void* getInterface(SlangUUID const& uuid) override
    {
        if (uuid == GUID::IID_IComputeCommandEncoder || uuid == GUID::IID_IResourceCommandEncoder ||
            uuid == ISlangUnknown::getTypeGuid())
            return this;
        return nullptr;
    }

public:
    virtual SLANG_NO_THROW void SLANG_MCALL endEncoding() override;
    void init(DeviceImpl* renderer, TransientResourceHeapImpl* transientHeap, CommandBufferImpl* cmdBuffer);

    virtual SLANG_NO_THROW Result SLANG_MCALL
    bindPipeline(IPipelineState* state, IShaderObject** outRootObject) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    bindPipelineWithRootObject(IPipelineState* state, IShaderObject* rootObject) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL dispatchCompute(int x, int y, int z) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL dispatchComputeIndirect(IBuffer* argBuffer, Offset offset) override;
};

struct BoundVertexBuffer
{
    RefPtr<BufferImpl> m_buffer;
    int m_offset;
};

class RenderCommandEncoderImpl : public IRenderCommandEncoder, public ResourceCommandEncoderImpl
{
public:
    SLANG_RHI_FORWARD_RESOURCE_COMMAND_ENCODER_IMPL(ResourceCommandEncoderImpl)
    virtual void* getInterface(SlangUUID const& uuid) override
    {
        if (uuid == GUID::IID_IRenderCommandEncoder || uuid == GUID::IID_IResourceCommandEncoder ||
            uuid == ISlangUnknown::getTypeGuid())
            return this;
        return nullptr;
    }

public:
    RefPtr<RenderPassLayoutImpl> m_renderPass;
    RefPtr<FramebufferImpl> m_framebuffer;

    std::vector<BoundVertexBuffer> m_boundVertexBuffers;

    RefPtr<BufferImpl> m_boundIndexBuffer;

    D3D12_VIEWPORT m_viewports[kMaxRTVCount];
    D3D12_RECT m_scissorRects[kMaxRTVCount];

    DXGI_FORMAT m_boundIndexFormat;
    UINT m_boundIndexOffset;

    D3D12_PRIMITIVE_TOPOLOGY_TYPE m_primitiveTopologyType;
    D3D12_PRIMITIVE_TOPOLOGY m_primitiveTopology;

    void init(
        DeviceImpl* renderer,
        TransientResourceHeapImpl* transientHeap,
        CommandBufferImpl* cmdBuffer,
        RenderPassLayoutImpl* renderPass,
        FramebufferImpl* framebuffer
    );

    virtual SLANG_NO_THROW Result SLANG_MCALL
    bindPipeline(IPipelineState* state, IShaderObject** outRootObject) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    bindPipelineWithRootObject(IPipelineState* state, IShaderObject* rootObject) override;

    virtual SLANG_NO_THROW void SLANG_MCALL setViewports(GfxCount count, const Viewport* viewports) override;

    virtual SLANG_NO_THROW void SLANG_MCALL setScissorRects(GfxCount count, const ScissorRect* rects) override;

    virtual SLANG_NO_THROW void SLANG_MCALL setPrimitiveTopology(PrimitiveTopology topology) override;

    virtual SLANG_NO_THROW void SLANG_MCALL
    setVertexBuffers(GfxIndex startSlot, GfxCount slotCount, IBuffer* const* buffers, const Offset* offsets) override;

    virtual SLANG_NO_THROW void SLANG_MCALL
    setIndexBuffer(IBuffer* buffer, Format indexFormat, Offset offset = 0) override;

    Result prepareDraw();
    virtual SLANG_NO_THROW Result SLANG_MCALL draw(GfxCount vertexCount, GfxIndex startVertex = 0) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    drawIndexed(GfxCount indexCount, GfxIndex startIndex = 0, GfxIndex baseVertex = 0) override;
    virtual SLANG_NO_THROW void SLANG_MCALL endEncoding() override;

    virtual SLANG_NO_THROW void SLANG_MCALL setStencilReference(uint32_t referenceValue) override;

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

class RayTracingCommandEncoderImpl : public IRayTracingCommandEncoder, public ResourceCommandEncoderImpl
{
public:
    SLANG_RHI_FORWARD_RESOURCE_COMMAND_ENCODER_IMPL(ResourceCommandEncoderImpl)
    virtual void* getInterface(SlangUUID const& uuid) override
    {
        if (uuid == GUID::IID_IRayTracingCommandEncoder || uuid == GUID::IID_IResourceCommandEncoder ||
            uuid == ISlangUnknown::getTypeGuid())
            return this;
        return nullptr;
    }

public:
    virtual SLANG_NO_THROW void SLANG_MCALL buildAccelerationStructure(
        const IAccelerationStructure::BuildDesc& desc,
        GfxCount propertyQueryCount,
        AccelerationStructureQueryDesc* queryDescs
    ) override;
    virtual SLANG_NO_THROW void SLANG_MCALL copyAccelerationStructure(
        IAccelerationStructure* dest,
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
    serializeAccelerationStructure(DeviceAddress dest, IAccelerationStructure* source) override;
    virtual SLANG_NO_THROW void SLANG_MCALL
    deserializeAccelerationStructure(IAccelerationStructure* dest, DeviceAddress source) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    bindPipeline(IPipelineState* state, IShaderObject** outRootObject) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    bindPipelineWithRootObject(IPipelineState* state, IShaderObject* rootObject) override
    {
        return bindPipelineWithRootObjectImpl(state, rootObject);
    }
    virtual SLANG_NO_THROW Result SLANG_MCALL
    dispatchRays(GfxIndex rayGenShaderIndex, IShaderTable* shaderTable, GfxCount width, GfxCount height, GfxCount depth)
        override;
    virtual SLANG_NO_THROW void SLANG_MCALL endEncoding() override {}
};

} // namespace rhi::d3d12
