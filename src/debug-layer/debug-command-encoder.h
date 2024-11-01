#pragma once

#include "debug-base.h"
#include "debug-shader-object.h"

namespace rhi::debug {

class DebugCommandEncoder : public DebugObject<ICommandEncoder>
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL;

public:
    DebugCommandEncoder(DebugContext* ctx);
    ICommandEncoder* getInterface(const Guid& guid);

    virtual SLANG_NO_THROW void SLANG_MCALL
    copyBuffer(IBuffer* dst, Offset dstOffset, IBuffer* src, Offset srcOffset, Size size) override;

    /// Copies texture from src to dst. If dstSubresource and srcSubresource has mipLevelCount override
    /// and layerCount override, the entire resource is being copied and dstOffset, srcOffset and extent
    /// arguments are ignored.
    virtual SLANG_NO_THROW void SLANG_MCALL copyTexture(
        ITexture* dst,
        SubresourceRange dstSubresource,
        Offset3D dstOffset,
        ITexture* src,
        SubresourceRange srcSubresource,
        Offset3D srcOffset,
        Extents extent
    ) override;

    /// Copies texture to a buffer. Each row is aligned to kTexturePitchAlignment.
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

    virtual SLANG_NO_THROW void SLANG_MCALL uploadTextureData(
        ITexture* dst,
        SubresourceRange subresourceRange,
        Offset3D offset,
        Extents extent,
        SubresourceData* subresourceData,
        GfxCount subresourceDataCount
    ) override;

    virtual SLANG_NO_THROW void SLANG_MCALL
    uploadBufferData(IBuffer* dst, Offset offset, Size size, void* data) override;

    virtual SLANG_NO_THROW void SLANG_MCALL clearBuffer(IBuffer* buffer, const BufferRange* range = nullptr) override;

    inline void clearBuffer(IBuffer* buffer, Offset offset, Size size)
    {
        BufferRange range = {offset, size};
        clearBuffer(buffer, &range);
    }

    virtual SLANG_NO_THROW void SLANG_MCALL clearTexture(
        ITexture* texture,
        const ClearValue& clearValue = ClearValue(),
        const SubresourceRange* subresourceRange = nullptr,
        bool clearDepth = true,
        bool clearStencil = true
    ) override;

    virtual SLANG_NO_THROW void SLANG_MCALL
    resolveQuery(IQueryPool* queryPool, GfxIndex index, GfxCount count, IBuffer* buffer, Offset offset) override;

    virtual SLANG_NO_THROW void SLANG_MCALL beginRenderPass(const RenderPassDesc& desc) override;
    virtual SLANG_NO_THROW void SLANG_MCALL endRenderPass() override;

    virtual SLANG_NO_THROW void SLANG_MCALL setRenderState(const RenderState& state) override;

    virtual SLANG_NO_THROW void SLANG_MCALL draw(const DrawArguments& args) override;
    virtual SLANG_NO_THROW void SLANG_MCALL drawIndexed(const DrawArguments& args) override;

    virtual SLANG_NO_THROW void SLANG_MCALL
    drawIndirect(GfxCount maxDrawCount, IBuffer* argBuffer, Offset argOffset, IBuffer* countBuffer, Offset countOffset)
        override;

    virtual SLANG_NO_THROW void SLANG_MCALL drawIndexedIndirect(
        GfxCount maxDrawCount,
        IBuffer* argBuffer,
        Offset argOffset,
        IBuffer* countBuffer,
        Offset countOffset
    ) override;

    virtual SLANG_NO_THROW void SLANG_MCALL drawMeshTasks(int x, int y, int z) override;

    virtual SLANG_NO_THROW void SLANG_MCALL beginComputePass() override;
    virtual SLANG_NO_THROW void SLANG_MCALL endComputePass() override;

    virtual SLANG_NO_THROW void SLANG_MCALL setComputeState(const ComputeState& state) override;

    virtual SLANG_NO_THROW void SLANG_MCALL dispatchCompute(int x, int y, int z) override;

    virtual SLANG_NO_THROW void SLANG_MCALL dispatchComputeIndirect(IBuffer* argBuffer, Offset offset) override;

    virtual SLANG_NO_THROW void SLANG_MCALL beginRayTracingPass() override;
    virtual SLANG_NO_THROW void SLANG_MCALL endRayTracingPass() override;

    virtual SLANG_NO_THROW void SLANG_MCALL setRayTracingState(const RayTracingState& state) override;

    /// Issues a dispatch command to start ray tracing workload with a ray tracing pipeline.
    /// `rayGenShaderIndex` specifies the index into the shader table that identifies the ray generation shader.
    virtual SLANG_NO_THROW void SLANG_MCALL
    dispatchRays(GfxIndex rayGenShaderIndex, GfxCount width, GfxCount height, GfxCount depth) override;

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

    virtual SLANG_NO_THROW void SLANG_MCALL setBufferState(IBuffer* buffer, ResourceState state) override;

    virtual SLANG_NO_THROW void SLANG_MCALL
    setTextureState(ITexture* texture, SubresourceRange subresourceRange, ResourceState state) override;

    inline void setTextureState(ITexture* texture, ResourceState state)
    {
        setTextureState(texture, kEntireTexture, state);
    }

    virtual SLANG_NO_THROW void SLANG_MCALL beginDebugEvent(const char* name, float rgbColor[3]) override;
    virtual SLANG_NO_THROW void SLANG_MCALL endDebugEvent() override;

    virtual SLANG_NO_THROW void SLANG_MCALL writeTimestamp(IQueryPool* queryPool, GfxIndex queryIndex) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL finish(ICommandBuffer** outCommandBuffer) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;

private:
    void requireOpen();
    void requireNoPass();
    void requireRenderPass();
    void requireComputePass();
    void requireRayTracingPass();

public:
    enum class EncoderState
    {
        Open,
        Finished,
    };

    enum class PassState
    {
        NoPass,
        RenderPass,
        ComputePass,
        RayTracingPass,
    };

    EncoderState m_state = EncoderState::Open;
    PassState m_passState = PassState::NoPass;
};

} // namespace rhi::debug
