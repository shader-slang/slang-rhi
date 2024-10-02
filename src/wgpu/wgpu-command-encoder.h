#pragma once

#include "wgpu-base.h"
#include "../pass-encoder-com-forward.h"

namespace rhi::wgpu {

struct RootBindingContext;

class PassEncoderImpl : public IPassEncoder
{
public:
    virtual void* getInterface(SlangUUID const& uuid);
    virtual SLANG_NO_THROW Result SLANG_MCALL queryInterface(SlangUUID const& uuid, void** outObject) override;
    virtual SLANG_NO_THROW uint32_t SLANG_MCALL addRef() override;
    virtual SLANG_NO_THROW uint32_t SLANG_MCALL release() override;

public:
    DeviceImpl* m_device = nullptr;
    CommandBufferImpl* m_commandBuffer = nullptr;
    WGPUCommandEncoder m_commandEncoder = nullptr;
    RefPtr<PipelineImpl> m_currentPipeline;

    virtual ~PassEncoderImpl();

    void init(CommandBufferImpl* commandBuffer);

    Result bindPipelineImpl(RootBindingContext& context);
    void endEncodingImpl();

    void uploadBufferDataImpl(IBuffer* buffer, Offset offset, Size size, void* data);

    Result setPipelineImpl(IPipeline* state, IShaderObject** outRootObject);
    Result setPipelineWithRootObjectImpl(IPipeline* state, IShaderObject* rootObject);

    // IPassEncoder implementation
    virtual SLANG_NO_THROW void SLANG_MCALL setBufferState(IBuffer* buffer, ResourceState state) override;
    virtual SLANG_NO_THROW void SLANG_MCALL
    setTextureState(ITexture* texture, SubresourceRange subresourceRange, ResourceState state) override;

    virtual SLANG_NO_THROW void SLANG_MCALL beginDebugEvent(const char* name, float rgbColor[3]) override;
    virtual SLANG_NO_THROW void SLANG_MCALL endDebugEvent() override;

    virtual SLANG_NO_THROW void SLANG_MCALL writeTimestamp(IQueryPool* pool, GfxIndex index) override;
};

class ResourcePassEncoderImpl : public IResourcePassEncoder, public PassEncoderImpl
{
public:
    SLANG_RHI_FORWARD_PASS_ENCODER_IMPL(PassEncoderImpl)
    virtual void* getInterface(SlangUUID const& uuid) override;

public:
    Result init(CommandBufferImpl* commandBuffer);

    // IResourcePassEncoder implementation

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
    virtual void* getInterface(SlangUUID const& uuid) override;

public:
    WGPURenderPassEncoder m_renderPassEncoder = nullptr;

    Result init(CommandBufferImpl* commandBuffer, const RenderPassDesc& renderPass);

    Result prepareDraw();

    // IRenderPassEncoder implementation

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
    virtual void* getInterface(SlangUUID const& uuid) override;

public:
    WGPUComputePassEncoder m_computePassEncoder = nullptr;

    Result init(CommandBufferImpl* commandBuffer);

    // IComputePassEncoder implementation

    virtual SLANG_NO_THROW void SLANG_MCALL end() override;

    virtual SLANG_NO_THROW Result SLANG_MCALL bindPipeline(IPipeline* pipeline, IShaderObject** outRootObject) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL
    bindPipelineWithRootObject(IPipeline* pipeline, IShaderObject* rootObject) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL dispatchCompute(int x, int y, int z) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL dispatchComputeIndirect(IBuffer* argBuffer, Offset offset) override;
};

} // namespace rhi::wgpu
