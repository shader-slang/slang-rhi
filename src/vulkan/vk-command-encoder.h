#pragma once

#include "vk-base.h"
#include "vk-pipeline.h"
#include "vk-shader-object.h"
#include "vk-shader-table.h"
#include "../state-tracking.h"

#include "core/static_vector.h"

#include <vector>

namespace rhi::vk {

class CommandEncoderImpl : public CommandEncoder
{
public:
    RefPtr<DeviceImpl> m_device;
    CommandQueueImpl* m_queue;
    RefPtr<CommandBufferImpl> m_commandBuffer;
    RefPtr<TransientResourceHeapImpl> m_transientHeap;
    VkCommandBuffer m_cmdBuffer = VK_NULL_HANDLE;

    StateTracking m_stateTracking;

    short_vector<RefPtr<TextureViewImpl>> m_renderTargetViews;
    short_vector<RefPtr<TextureViewImpl>> m_resolveTargetViews;
    RefPtr<TextureViewImpl> m_depthStencilView;

    bool m_renderPassActive = false;
    bool m_renderStateValid = false;
    RenderState m_renderState;
    RefPtr<RenderPipelineImpl> m_renderPipeline;

    bool m_computeStateValid = false;
    ComputeState m_computeState;
    RefPtr<ComputePipelineImpl> m_computePipeline;

    bool m_rayTracingStateValid = false;
    RayTracingState m_rayTracingState;
    RefPtr<RayTracingPipelineImpl> m_rayTracingPipeline;
    RefPtr<ShaderTableImpl> m_shaderTable;

    uint64_t m_rayGenTableAddr = 0;
    VkStridedDeviceAddressRegionKHR m_raygenSBT;
    VkStridedDeviceAddressRegionKHR m_missSBT;
    VkStridedDeviceAddressRegionKHR m_hitSBT;
    VkStridedDeviceAddressRegionKHR m_callableSBT;

    RefPtr<RootShaderObjectImpl> m_rootObject;

    bool m_descriptorHeapsBound = false;

    Result init(DeviceImpl* device, CommandQueueImpl* queue);

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
        uint32_t propertyQueryCount,
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
    void endPassEncoder();

    void requireBufferState(BufferImpl* buffer, ResourceState state);
    void requireTextureState(TextureImpl* texture, SubresourceRange subresourceRange, ResourceState state);
    void commitBarriers();

    void _memoryBarrier(
        int count,
        IAccelerationStructure* const* structures,
        AccessFlag srcAccess,
        AccessFlag destAccess
    );

    void _queryAccelerationStructureProperties(
        GfxCount accelerationStructureCount,
        IAccelerationStructure* const* accelerationStructures,
        GfxCount queryCount,
        AccelerationStructureQueryDesc* queryDescs
    );

    Result bindRootObject(
        RootShaderObjectImpl* rootObject,
        RootShaderObjectLayout* rootObjectLayout,
        VkPipelineBindPoint bindPoint
    );
};

#if 0
class PassEncoderImpl : public IPassEncoder
{
public:
    CommandBufferImpl* m_commandBuffer;
    VkCommandBuffer m_vkCommandBuffer;
    VkCommandBuffer m_vkPreCommandBuffer = VK_NULL_HANDLE;
    VkPipeline m_boundPipelines[3] = {};
    DeviceImpl* m_device = nullptr;
    RefPtr<Pipeline> m_currentPipeline;

    VulkanApi* m_api;

    void init(CommandBufferImpl* commandBuffer);

    void endEncodingImpl();

    void _uploadBufferData(
        VulkanApi* api,
        VkCommandBuffer commandBuffer,
        TransientResourceHeapImpl* transientHeap,
        BufferImpl* buffer,
        Offset offset,
        Size size,
        void* data
    );

    void uploadBufferDataImpl(IBuffer* buffer, Offset offset, Size size, void* data);

    Result bindRootShaderObjectImpl(RootShaderObjectImpl* rootShaderObject, VkPipelineBindPoint bindPoint);

    Result setPipelineImpl(IPipeline* state, IShaderObject** outRootObject);

    Result setPipelineWithRootObjectImpl(IPipeline* state, IShaderObject* rootObject);

    Result bindRenderState(VkPipelineBindPoint pipelineBindPoint);
};
#endif

} // namespace rhi::vk
