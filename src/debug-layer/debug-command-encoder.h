#pragma once

#include "debug-base.h"
#include "debug-shader-object.h"

namespace rhi::debug {

class DebugRenderPassEncoder : public UnownedDebugObject<IRenderPassEncoder>
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_QUERY_INTERFACE;
    IRenderPassEncoder* getInterface(const Guid& guid);
    virtual SLANG_NO_THROW uint32_t SLANG_MCALL addRef() override { return 1; }
    virtual SLANG_NO_THROW uint32_t SLANG_MCALL release() override { return 1; }

public:
    DebugCommandEncoder* m_commandEncoder;

    DebugRenderPassEncoder(DebugContext* ctx, DebugCommandEncoder* commandEncoder);

    virtual SLANG_NO_THROW void SLANG_MCALL setRenderState(const RenderState& state) override;
    virtual SLANG_NO_THROW void SLANG_MCALL draw(const DrawArguments& args) override;
    virtual SLANG_NO_THROW void SLANG_MCALL drawIndexed(const DrawArguments& args) override;
    virtual SLANG_NO_THROW void SLANG_MCALL drawIndirect(
        uint32_t maxDrawCount,
        IBuffer* argBuffer,
        uint64_t argOffset,
        IBuffer* countBuffer = nullptr,
        uint64_t countOffset = 0
    ) override;
    virtual SLANG_NO_THROW void SLANG_MCALL drawIndexedIndirect(
        uint32_t maxDrawCount,
        IBuffer* argBuffer,
        uint64_t argOffset,
        IBuffer* countBuffer = nullptr,
        uint64_t countOffset = 0
    ) override;
    virtual SLANG_NO_THROW void SLANG_MCALL drawMeshTasks(uint32_t x, uint32_t y, uint32_t z) override;

    virtual SLANG_NO_THROW void SLANG_MCALL pushDebugGroup(const char* name, float rgbColor[3]) override;
    virtual SLANG_NO_THROW void SLANG_MCALL popDebugGroup() override;
    virtual SLANG_NO_THROW void SLANG_MCALL insertDebugMarker(const char* name, float rgbColor[3]) override;

    virtual SLANG_NO_THROW void SLANG_MCALL end() override;
};

class DebugComputePassEncoder : public UnownedDebugObject<IComputePassEncoder>
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_QUERY_INTERFACE;
    IComputePassEncoder* getInterface(const Guid& guid);
    virtual SLANG_NO_THROW uint32_t SLANG_MCALL addRef() override { return 1; }
    virtual SLANG_NO_THROW uint32_t SLANG_MCALL release() override { return 1; }

public:
    DebugCommandEncoder* m_commandEncoder;

    DebugComputePassEncoder(DebugContext* ctx, DebugCommandEncoder* commandEncoder);

    virtual SLANG_NO_THROW void SLANG_MCALL setComputeState(const ComputeState& state) override;
    virtual SLANG_NO_THROW void SLANG_MCALL dispatchCompute(uint32_t x, uint32_t y, uint32_t z) override;
    virtual SLANG_NO_THROW void SLANG_MCALL dispatchComputeIndirect(IBuffer* argBuffer, uint64_t offset) override;

    virtual SLANG_NO_THROW void SLANG_MCALL pushDebugGroup(const char* name, float rgbColor[3]) override;
    virtual SLANG_NO_THROW void SLANG_MCALL popDebugGroup() override;
    virtual SLANG_NO_THROW void SLANG_MCALL insertDebugMarker(const char* name, float rgbColor[3]) override;

    virtual SLANG_NO_THROW void SLANG_MCALL end() override;
};

class DebugRayTracingPassEncoder : public UnownedDebugObject<IRayTracingPassEncoder>
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_QUERY_INTERFACE;
    IRayTracingPassEncoder* getInterface(const Guid& guid);
    virtual SLANG_NO_THROW uint32_t SLANG_MCALL addRef() override { return 1; }
    virtual SLANG_NO_THROW uint32_t SLANG_MCALL release() override { return 1; }

public:
    DebugCommandEncoder* m_commandEncoder;

    DebugRayTracingPassEncoder(DebugContext* ctx, DebugCommandEncoder* commandEncoder);

    virtual SLANG_NO_THROW void SLANG_MCALL setRayTracingState(const RayTracingState& state) override;
    virtual SLANG_NO_THROW void SLANG_MCALL
    dispatchRays(uint32_t rayGenShaderIndex, uint32_t width, uint32_t height, uint32_t depth) override;

    virtual SLANG_NO_THROW void SLANG_MCALL pushDebugGroup(const char* name, float rgbColor[3]) override;
    virtual SLANG_NO_THROW void SLANG_MCALL popDebugGroup() override;
    virtual SLANG_NO_THROW void SLANG_MCALL insertDebugMarker(const char* name, float rgbColor[3]) override;

    virtual SLANG_NO_THROW void SLANG_MCALL end() override;
};

class DebugCommandEncoder : public DebugObject<ICommandEncoder>
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL;
    ICommandEncoder* getInterface(const Guid& guid);

public:
    DebugCommandEncoder(DebugContext* ctx);

    virtual SLANG_NO_THROW IRenderPassEncoder* SLANG_MCALL beginRenderPass(const RenderPassDesc& desc) override;
    virtual SLANG_NO_THROW IComputePassEncoder* SLANG_MCALL beginComputePass() override;
    virtual SLANG_NO_THROW IRayTracingPassEncoder* SLANG_MCALL beginRayTracingPass() override;

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

    virtual SLANG_NO_THROW void SLANG_MCALL pushDebugGroup(const char* name, float rgbColor[3]) override;
    virtual SLANG_NO_THROW void SLANG_MCALL popDebugGroup() override;
    virtual SLANG_NO_THROW void SLANG_MCALL insertDebugMarker(const char* name, float rgbColor[3]) override;

    virtual SLANG_NO_THROW void SLANG_MCALL writeTimestamp(IQueryPool* queryPool, GfxIndex queryIndex) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL finish(ICommandBuffer** outCommandBuffer) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;

public:
    void requireOpen();
    void requireNoPass();
    void requireRenderPass();
    void requireComputePass();
    void requireRayTracingPass();

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

    DebugRenderPassEncoder m_renderPassEncoder;
    DebugComputePassEncoder m_computePassEncoder;
    DebugRayTracingPassEncoder m_rayTracingPassEncoder;
};

} // namespace rhi::debug
