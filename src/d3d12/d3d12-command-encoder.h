#pragma once

#include "d3d12-base.h"
#include "d3d12-buffer.h"
#include "d3d12-submitter.h"
#include "d3d12-texture-view.h"

#include "core/static_vector.h"

namespace rhi::d3d12 {

static const Int kMaxRTVCount = 8;

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
    // IPassEncoder implementation
    virtual SLANG_NO_THROW void SLANG_MCALL setBufferState(IBuffer* buffer, ResourceState state) override;
    virtual SLANG_NO_THROW void SLANG_MCALL
    setTextureState(ITexture* texture, SubresourceRange subresourceRange, ResourceState state) override;

    virtual SLANG_NO_THROW void SLANG_MCALL beginDebugEvent(const char* name, float rgbColor[3]) override;
    virtual SLANG_NO_THROW void SLANG_MCALL endDebugEvent() override;

    virtual SLANG_NO_THROW void SLANG_MCALL writeTimestamp(IQueryPool* pool, GfxIndex index) override;

public:
    bool m_isOpen = false;
    bool m_bindingDirty = true;
    CommandBufferImpl* m_commandBuffer;
    TransientResourceHeapImpl* m_transientHeap;
    DeviceImpl* m_device;
    ID3D12Device* m_d3dDevice;
    ID3D12GraphicsCommandList* m_d3dCmdList;
    ID3D12GraphicsCommandList6* m_d3dCmdList6;
    ID3D12GraphicsCommandList* m_preCmdList = nullptr;

    RefPtr<Pipeline> m_currentPipeline;

    void init(CommandBufferImpl* commandBuffer);

    void endEncodingImpl() { m_isOpen = false; }

    Result bindPipelineImpl(IPipeline* pipeline, IShaderObject** outRootObject);

    Result bindPipelineWithRootObjectImpl(IPipeline* pipeline, IShaderObject* rootObject);

    /// Specializes the pipeline according to current root-object argument values,
    /// applys the root object bindings and binds the pipeline state.
    /// The newly specialized pipeline is held alive by the pipeline cache so users of
    /// `newPipeline` do not need to maintain its lifespan.
    Result _bindRenderState(Submitter* submitter, RefPtr<Pipeline>& newPipeline);
};

class ResourcePassEncoderImpl : public IResourcePassEncoder, public PassEncoderImpl
{
public:
    SLANG_RHI_FORWARD_PASS_ENCODER_IMPL(PassEncoderImpl)
    virtual void* getInterface(SlangUUID const& uuid) override
    {
        if (uuid == GUID::IID_IRenderPassEncoder || uuid == GUID::IID_IPassEncoder ||
            uuid == ISlangUnknown::getTypeGuid())
            return this;
        return nullptr;
    }

public:
    virtual SLANG_NO_THROW void SLANG_MCALL
    copyBuffer(IBuffer* dst, Offset dstOffset, IBuffer* src, Offset srcOffset, Size size) override;
    virtual SLANG_NO_THROW void SLANG_MCALL
    uploadBufferData(IBuffer* dst, Offset offset, Size size, void* data) override;
    virtual SLANG_NO_THROW void SLANG_MCALL end() override {}
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

    virtual SLANG_NO_THROW void SLANG_MCALL clearBuffer(IBuffer* view, const BufferRange* range = nullptr) override;

    virtual SLANG_NO_THROW void SLANG_MCALL clearTexture(
        ITexture* texture,
        const ClearValue& clearValue,
        const SubresourceRange* subresourceRange,
        bool clearDepth,
        bool clearStencil
    ) override;

    virtual SLANG_NO_THROW void SLANG_MCALL
    resolveQuery(IQueryPool* queryPool, GfxIndex index, GfxCount count, IBuffer* buffer, Offset offset) override;

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
};

struct BoundVertexBuffer
{
    RefPtr<BufferImpl> m_buffer;
    int m_offset;
};

class RenderPassEncoderImpl : public IRenderPassEncoder, public PassEncoderImpl
{
public:
    SLANG_RHI_FORWARD_PASS_ENCODER_IMPL(PassEncoderImpl)
    virtual void* getInterface(SlangUUID const& uuid) override
    {
        if (uuid == GUID::IID_IRenderPassEncoder || uuid == GUID::IID_IPassEncoder ||
            uuid == ISlangUnknown::getTypeGuid())
            return this;
        return nullptr;
    }

public:
    short_vector<RefPtr<TextureViewImpl>> m_renderTargetViews;
    short_vector<RefPtr<TextureViewImpl>> m_resolveTargetViews;
    RefPtr<TextureViewImpl> m_depthStencilView;

    std::vector<BoundVertexBuffer> m_boundVertexBuffers;
    RefPtr<BufferImpl> m_boundIndexBuffer;

    D3D12_VIEWPORT m_viewports[kMaxRTVCount];
    D3D12_RECT m_scissorRects[kMaxRTVCount];

    DXGI_FORMAT m_boundIndexFormat;
    UINT m_boundIndexOffset;

    void init(
        DeviceImpl* device,
        TransientResourceHeapImpl* transientHeap,
        CommandBufferImpl* cmdBuffer,
        const RenderPassDesc& desc
    );

    virtual SLANG_NO_THROW Result SLANG_MCALL bindPipeline(IPipeline* state, IShaderObject** outRootObject) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    bindPipelineWithRootObject(IPipeline* state, IShaderObject* rootObject) override;

    virtual SLANG_NO_THROW void SLANG_MCALL setViewports(GfxCount count, const Viewport* viewports) override;

    virtual SLANG_NO_THROW void SLANG_MCALL setScissorRects(GfxCount count, const ScissorRect* rects) override;

    virtual SLANG_NO_THROW void SLANG_MCALL
    setVertexBuffers(GfxIndex startSlot, GfxCount slotCount, IBuffer* const* buffers, const Offset* offsets) override;

    virtual SLANG_NO_THROW void SLANG_MCALL
    setIndexBuffer(IBuffer* buffer, IndexFormat indexFormat, Offset offset = 0) override;

    Result prepareDraw();

    virtual SLANG_NO_THROW Result SLANG_MCALL draw(GfxCount vertexCount, GfxIndex startVertex = 0) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL
    drawIndexed(GfxCount indexCount, GfxIndex startIndex = 0, GfxIndex baseVertex = 0) override;
    virtual SLANG_NO_THROW void SLANG_MCALL end() override;

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

class ComputePassEncoderImpl : public IComputePassEncoder, public PassEncoderImpl
{
public:
    SLANG_RHI_FORWARD_PASS_ENCODER_IMPL(PassEncoderImpl)
    virtual void* getInterface(SlangUUID const& uuid) override
    {
        if (uuid == GUID::IID_IComputePassEncoder || uuid == GUID::IID_IPassEncoder ||
            uuid == ISlangUnknown::getTypeGuid())
            return this;
        return nullptr;
    }

public:
    virtual SLANG_NO_THROW void SLANG_MCALL end() override;
    void init(DeviceImpl* device, TransientResourceHeapImpl* transientHeap, CommandBufferImpl* cmdBuffer);

    virtual SLANG_NO_THROW Result SLANG_MCALL bindPipeline(IPipeline* state, IShaderObject** outRootObject) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    bindPipelineWithRootObject(IPipeline* state, IShaderObject* rootObject) override;

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
            return this;
        return nullptr;
    }

public:
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
    virtual SLANG_NO_THROW void SLANG_MCALL end() override {}
};

} // namespace rhi::d3d12
