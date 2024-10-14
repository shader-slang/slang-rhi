#pragma once

#include "metal-base.h"
#include "metal-pipeline.h"

#include "core/short_vector.h"

namespace rhi::metal {

class CommandEncoderImpl : public CommandEncoder
{
public:
    DeviceImpl* m_device;
    CommandQueueImpl* m_queue;
    RefPtr<CommandBufferImpl> m_commandBuffer;
    NS::SharedPtr<MTL::CommandBuffer> m_metalCommandBuffer;

    RenderState m_renderState;
    RefPtr<RenderPipelineImpl> m_renderPipeline;
    bool m_useDepthStencil = false;
    RefPtr<BufferImpl> m_indexBuffer;
    MTL::IndexType m_indexType;
    NS::UInteger m_indexSize;
    NS::UInteger m_indexBufferOffset;

    ComputeState m_computeState;
    RefPtr<ComputePipelineImpl> m_computePipeline;

    RayTracingState m_rayTracingState;
    RefPtr<RayTracingPipelineImpl> m_rayTracingPipeline;

    RefPtr<RootShaderObjectImpl> m_rootObject;

    std::vector<RefPtr<RefObject>> m_resources;

    NS::SharedPtr<MTL::RenderCommandEncoder> m_metalRenderCommandEncoder;
    NS::SharedPtr<MTL::ComputeCommandEncoder> m_metalComputeCommandEncoder;
    NS::SharedPtr<MTL::AccelerationStructureCommandEncoder> m_metalAccelerationStructureCommandEncoder;
    NS::SharedPtr<MTL::BlitCommandEncoder> m_metalBlitCommandEncoder;

    void init(DeviceImpl* device, CommandQueueImpl* queue);

    virtual Result createRootShaderObject(ShaderProgram* program, ShaderObjectBase** outObject) override;

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
    virtual SLANG_NO_THROW void SLANG_MCALL drawIndirect(
        GfxCount maxDrawCount,
        IBuffer* argBuffer,
        Offset argOffset,
        IBuffer* countBuffer = nullptr,
        Offset countOffset = 0
    ) override;
    virtual SLANG_NO_THROW void SLANG_MCALL drawIndexedIndirect(
        GfxCount maxDrawCount,
        IBuffer* argBuffer,
        Offset argOffset,
        IBuffer* countBuffer = nullptr,
        Offset countOffset = 0
    ) override;
    virtual SLANG_NO_THROW void SLANG_MCALL drawMeshTasks(int x, int y, int z) override;

    virtual SLANG_NO_THROW void SLANG_MCALL setComputeState(const ComputeState& state) override;

    virtual SLANG_NO_THROW void SLANG_MCALL dispatchCompute(int x, int y, int z) override;

    virtual SLANG_NO_THROW void SLANG_MCALL dispatchComputeIndirect(IBuffer* argBuffer, Offset offset) override;

    virtual SLANG_NO_THROW void SLANG_MCALL setRayTracingState(const RayTracingState& state) override;

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

    virtual SLANG_NO_THROW void SLANG_MCALL beginDebugEvent(const char* name, float rgbColor[3]) override;

    virtual SLANG_NO_THROW void SLANG_MCALL endDebugEvent() override;

    virtual SLANG_NO_THROW void SLANG_MCALL writeTimestamp(IQueryPool* pool, GfxIndex index) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL finish(ICommandBuffer** outCommandBuffer) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;

private:
    MTL::RenderCommandEncoder* getMetalRenderCommandEncoder(MTL::RenderPassDescriptor* renderPassDesc);
    MTL::ComputeCommandEncoder* getMetalComputeCommandEncoder();
    MTL::AccelerationStructureCommandEncoder* getMetalAccelerationStructureCommandEncoder();
    MTL::BlitCommandEncoder* getMetalBlitCommandEncoder();
    void endMetalCommandEncoder();
};

} // namespace rhi::metal
