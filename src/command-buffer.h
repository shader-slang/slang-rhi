#pragma once

#include <slang-rhi.h>

#include "slang-context.h"
#include "shader-object.h"

#include "core/common.h"
#include "core/arena-allocator.h"

#include "reference.h"
#include "command-list.h"
#include "device-child.h"

#include "rhi-shared-fwd.h"

#include <set>

namespace rhi {

struct BindingData
{};

class CommandQueue : public ICommandQueue, public DeviceChild
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL
    ICommandQueue* getInterface(const Guid& guid)
    {
        if (guid == ISlangUnknown::getTypeGuid() || guid == ICommandQueue::getTypeGuid())
            return static_cast<ICommandQueue*>(this);
        return nullptr;
    }

public:
    CommandQueue(Device* device, QueueType type)
        : DeviceChild(device)
    {
        m_type = type;
    }

    virtual void makeExternal() override { establishStrongReferenceToDevice(); }
    virtual void makeInternal() override { breakStrongReferenceToDevice(); }

    // ICommandQueue implementation
    virtual SLANG_NO_THROW QueueType SLANG_MCALL getType() override { return m_type; }

public:
    QueueType m_type;
};

class RenderPassEncoder : public IRenderPassEncoder
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_QUERY_INTERFACE
    IRenderPassEncoder* getInterface(const Guid& guid);
    virtual SLANG_NO_THROW uint32_t SLANG_MCALL addRef() override { return 1; }
    virtual SLANG_NO_THROW uint32_t SLANG_MCALL release() override { return 1; }

public:
    CommandEncoder* m_commandEncoder;
    ComPtr<IRenderPipeline> m_pipeline;
    RefPtr<RootShaderObject> m_rootObject;
    RenderState m_renderState;
    /// Command list, nullptr if pass encoder is not active.
    CommandList* m_commandList = nullptr;

    RenderPassEncoder(CommandEncoder* commandEncoder);

    void writeRenderState();

    // IRenderPassEncoder implementation
    virtual SLANG_NO_THROW IShaderObject* SLANG_MCALL bindPipeline(IRenderPipeline* pipeline) override;
    virtual SLANG_NO_THROW void SLANG_MCALL bindPipeline(IRenderPipeline* pipeline, IShaderObject* rootObject) override;
    virtual SLANG_NO_THROW void SLANG_MCALL setRenderState(const RenderState& state) override;
    virtual SLANG_NO_THROW void SLANG_MCALL draw(const DrawArguments& args) override;
    virtual SLANG_NO_THROW void SLANG_MCALL drawIndexed(const DrawArguments& args) override;
    virtual SLANG_NO_THROW void SLANG_MCALL drawIndirect(
        uint32_t maxDrawCount,
        BufferOffsetPair argBuffer,
        BufferOffsetPair countBuffer
    ) override;
    virtual SLANG_NO_THROW void SLANG_MCALL drawIndexedIndirect(
        uint32_t maxDrawCount,
        BufferOffsetPair argBuffer,
        BufferOffsetPair countBuffer
    ) override;
    virtual SLANG_NO_THROW void SLANG_MCALL drawMeshTasks(uint32_t x, uint32_t y, uint32_t z) override;

    // IPassEncoder implementation
    virtual SLANG_NO_THROW void SLANG_MCALL pushDebugGroup(const char* name, const MarkerColor& color) override;
    virtual SLANG_NO_THROW void SLANG_MCALL popDebugGroup() override;
    virtual SLANG_NO_THROW void SLANG_MCALL insertDebugMarker(const char* name, const MarkerColor& color) override;

    virtual SLANG_NO_THROW void SLANG_MCALL writeTimestamp(IQueryPool* queryPool, uint32_t queryIndex) override;

    virtual SLANG_NO_THROW void SLANG_MCALL end() override;
};

class ComputePassEncoder : public IComputePassEncoder
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_QUERY_INTERFACE
    IComputePassEncoder* getInterface(const Guid& guid);
    virtual SLANG_NO_THROW uint32_t SLANG_MCALL addRef() override { return 1; }
    virtual SLANG_NO_THROW uint32_t SLANG_MCALL release() override { return 1; }

public:
    CommandEncoder* m_commandEncoder = nullptr;
    ComPtr<IComputePipeline> m_pipeline;
    RefPtr<RootShaderObject> m_rootObject;
    /// Command list, nullptr if pass encoder is not active.
    CommandList* m_commandList;

    ComputePassEncoder(CommandEncoder* commandEncoder);

    void writeComputeState();

    // IComputePassEncoder implementation
    virtual SLANG_NO_THROW IShaderObject* SLANG_MCALL bindPipeline(IComputePipeline* pipeline) override;
    virtual SLANG_NO_THROW void SLANG_MCALL bindPipeline(
        IComputePipeline* pipeline,
        IShaderObject* rootObject
    ) override;
    virtual SLANG_NO_THROW void SLANG_MCALL dispatchCompute(uint32_t x, uint32_t y, uint32_t z) override;
    virtual SLANG_NO_THROW void SLANG_MCALL dispatchComputeIndirect(BufferOffsetPair argBuffer) override;

    // IPassEncoder implementation
    virtual SLANG_NO_THROW void SLANG_MCALL pushDebugGroup(const char* name, const MarkerColor& color) override;
    virtual SLANG_NO_THROW void SLANG_MCALL popDebugGroup() override;
    virtual SLANG_NO_THROW void SLANG_MCALL insertDebugMarker(const char* name, const MarkerColor& color) override;

    virtual SLANG_NO_THROW void SLANG_MCALL writeTimestamp(IQueryPool* queryPool, uint32_t queryIndex) override;

    virtual SLANG_NO_THROW void SLANG_MCALL end() override;
};

class RayTracingPassEncoder : public IRayTracingPassEncoder
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_QUERY_INTERFACE
    IRayTracingPassEncoder* getInterface(const Guid& guid);
    virtual SLANG_NO_THROW uint32_t SLANG_MCALL addRef() override { return 1; }
    virtual SLANG_NO_THROW uint32_t SLANG_MCALL release() override { return 1; }

public:
    CommandEncoder* m_commandEncoder = nullptr;
    ComPtr<IRayTracingPipeline> m_pipeline;
    ComPtr<IShaderTable> m_shaderTable;
    RefPtr<RootShaderObject> m_rootObject;
    /// Command list, nullptr if pass encoder is not active.
    CommandList* m_commandList;

    RayTracingPassEncoder(CommandEncoder* commandEncoder);

    void writeRayTracingState();

    // IRayTracingPassEncoder implementation
    virtual SLANG_NO_THROW IShaderObject* SLANG_MCALL bindPipeline(
        IRayTracingPipeline* pipeline,
        IShaderTable* shaderTable
    ) override;
    virtual SLANG_NO_THROW void SLANG_MCALL bindPipeline(
        IRayTracingPipeline* pipeline,
        IShaderTable* shaderTable,
        IShaderObject* rootObject
    ) override;
    virtual SLANG_NO_THROW void SLANG_MCALL dispatchRays(
        uint32_t rayGenShaderIndex,
        uint32_t width,
        uint32_t height,
        uint32_t depth
    ) override;

    // IPassEncoder implementation
    virtual SLANG_NO_THROW void SLANG_MCALL pushDebugGroup(const char* name, const MarkerColor& color) override;
    virtual SLANG_NO_THROW void SLANG_MCALL popDebugGroup() override;
    virtual SLANG_NO_THROW void SLANG_MCALL insertDebugMarker(const char* name, const MarkerColor& color) override;

    virtual SLANG_NO_THROW void SLANG_MCALL writeTimestamp(IQueryPool* queryPool, uint32_t queryIndex) override;

    virtual SLANG_NO_THROW void SLANG_MCALL end() override;
};

class CommandEncoder : public ICommandEncoder, public DeviceChild
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL
    ICommandEncoder* getInterface(const Guid& guid);

public:
    // Current command list to write to. Must be set by the derived class.
    CommandList* m_commandList = nullptr;

    RenderPassEncoder m_renderPassEncoder;
    ComputePassEncoder m_computePassEncoder;
    RayTracingPassEncoder m_rayTracingPassEncoder;

    // List of persisted pipeline specialization data.
    // This is populated during command encoding and later used when asynchronously resolving pipelines.
    std::vector<RefPtr<ExtendedShaderObjectTypeListObject>> m_pipelineSpecializationArgs;

    CommandEncoder(Device* device)
        : DeviceChild(device)
        , m_renderPassEncoder(this)
        , m_computePassEncoder(this)
        , m_rayTracingPassEncoder(this)
    {
    }

    virtual Result getBindingData(RootShaderObject* rootObject, BindingData*& outBindingData) = 0;

    Result getPipelineSpecializationArgs(
        IPipeline* pipeline,
        IShaderObject* object,
        ExtendedShaderObjectTypeListObject*& outSpecializationArgs
    );
    Result resolvePipelines(Device* device);

    // ICommandEncoder implementation
    virtual SLANG_NO_THROW IRenderPassEncoder* SLANG_MCALL beginRenderPass(const RenderPassDesc& desc) override;
    virtual SLANG_NO_THROW IComputePassEncoder* SLANG_MCALL beginComputePass() override;
    virtual SLANG_NO_THROW IRayTracingPassEncoder* SLANG_MCALL beginRayTracingPass() override;

    virtual SLANG_NO_THROW void SLANG_MCALL copyBuffer(
        IBuffer* dst,
        Offset dstOffset,
        IBuffer* src,
        Offset srcOffset,
        Size size
    ) override;

    virtual SLANG_NO_THROW void SLANG_MCALL copyTexture(
        ITexture* dst,
        SubresourceRange dstSubresource,
        Offset3D dstOffset,
        ITexture* src,
        SubresourceRange srcSubresource,
        Offset3D srcOffset,
        Extent3D extent
    ) override;

    virtual SLANG_NO_THROW void SLANG_MCALL copyTextureToBuffer(
        IBuffer* dst,
        Offset dstOffset,
        Size dstSize,
        Size dstRowPitch,
        ITexture* src,
        uint32_t srcLayer,
        uint32_t srcMip,
        Offset3D srcOffset,
        Extent3D extent
    ) override;

    virtual SLANG_NO_THROW void SLANG_MCALL copyBufferToTexture(
        ITexture* dst,
        uint32_t dstLayer,
        uint32_t dstMip,
        Offset3D dstOffset,
        IBuffer* src,
        Offset srcOffset,
        Size srcSize,
        Size srcRowPitch,
        Extent3D extent
    ) override;


    virtual SLANG_NO_THROW Result SLANG_MCALL uploadTextureData(
        ITexture* dst,
        SubresourceRange subresourceRange,
        Offset3D offset,
        Extent3D extent,
        const SubresourceData* subresourceData,
        uint32_t subresourceDataCount
    ) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL uploadBufferData(
        IBuffer* dst,
        Offset offset,
        Size size,
        const void* data
    ) override;

    virtual SLANG_NO_THROW void SLANG_MCALL clearBuffer(IBuffer* buffer, BufferRange range) override;

    virtual SLANG_NO_THROW void SLANG_MCALL clearTextureFloat(
        ITexture* texture,
        SubresourceRange subresourceRange,
        float clearValue[4]
    ) override;

    virtual SLANG_NO_THROW void SLANG_MCALL clearTextureUint(
        ITexture* texture,
        SubresourceRange subresourceRange,
        uint32_t clearValue[4]
    ) override;

    virtual SLANG_NO_THROW void SLANG_MCALL clearTextureSint(
        ITexture* texture,
        SubresourceRange subresourceRange,
        int32_t clearValue[4]
    ) override;

    virtual SLANG_NO_THROW void SLANG_MCALL clearTextureDepthStencil(
        ITexture* texture,
        SubresourceRange subresourceRange,
        bool clearDepth,
        float depthValue,
        bool clearStencil,
        uint8_t stencilValue
    ) override;

    virtual SLANG_NO_THROW void SLANG_MCALL resolveQuery(
        IQueryPool* queryPool,
        uint32_t index,
        uint32_t count,
        IBuffer* buffer,
        uint64_t offset
    ) override;

    virtual SLANG_NO_THROW void SLANG_MCALL buildAccelerationStructure(
        const AccelerationStructureBuildDesc& desc,
        IAccelerationStructure* dst,
        IAccelerationStructure* src,
        BufferOffsetPair scratchBuffer,
        uint32_t propertyQueryCount,
        const AccelerationStructureQueryDesc* queryDescs
    ) override;

    virtual SLANG_NO_THROW void SLANG_MCALL copyAccelerationStructure(
        IAccelerationStructure* dst,
        IAccelerationStructure* src,
        AccelerationStructureCopyMode mode
    ) override;

    virtual SLANG_NO_THROW void SLANG_MCALL queryAccelerationStructureProperties(
        uint32_t accelerationStructureCount,
        IAccelerationStructure** accelerationStructures,
        uint32_t queryCount,
        const AccelerationStructureQueryDesc* queryDescs
    ) override;

    virtual SLANG_NO_THROW void SLANG_MCALL serializeAccelerationStructure(
        BufferOffsetPair dst,
        IAccelerationStructure* src
    ) override;

    virtual SLANG_NO_THROW void SLANG_MCALL deserializeAccelerationStructure(
        IAccelerationStructure* dst,
        BufferOffsetPair src
    ) override;

    virtual SLANG_NO_THROW void SLANG_MCALL executeClusterOperation(const ClusterOperationDesc& desc) override;

    virtual SLANG_NO_THROW void SLANG_MCALL convertCooperativeVectorMatrix(
        IBuffer* dstBuffer,
        const CooperativeVectorMatrixDesc* dstDescs,
        IBuffer* srcBuffer,
        const CooperativeVectorMatrixDesc* srcDescs,
        uint32_t matrixCount
    ) override;

    virtual SLANG_NO_THROW void SLANG_MCALL setBufferState(IBuffer* buffer, ResourceState state) override;

    virtual SLANG_NO_THROW void SLANG_MCALL setTextureState(
        ITexture* texture,
        SubresourceRange subresourceRange,
        ResourceState state
    ) override;

    virtual SLANG_NO_THROW void SLANG_MCALL globalBarrier() override;

    virtual SLANG_NO_THROW void SLANG_MCALL pushDebugGroup(const char* name, const MarkerColor& color) override;
    virtual SLANG_NO_THROW void SLANG_MCALL popDebugGroup() override;
    virtual SLANG_NO_THROW void SLANG_MCALL insertDebugMarker(const char* name, const MarkerColor& color) override;

    virtual SLANG_NO_THROW void SLANG_MCALL writeTimestamp(IQueryPool* queryPool, uint32_t queryIndex) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL finish(ICommandBuffer** outCommandBuffer) override;
};

class CommandBuffer : public ICommandBuffer, public DeviceChild
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL
    ICommandBuffer* getInterface(const Guid& guid);

public:
    CommandBuffer(Device* device)
        : DeviceChild(device)
        , m_commandList(m_allocator, m_trackedObjects)
    {
    }
    virtual ~CommandBuffer() = default;

    virtual void makeExternal() override { establishStrongReferenceToDevice(); }
    virtual void makeInternal() override { breakStrongReferenceToDevice(); }

    virtual Result reset()
    {
        m_commandList.reset();
        m_allocator.reset();
        m_trackedObjects.clear();
        return SLANG_OK;
    }

    ArenaAllocator m_allocator;
    CommandList m_commandList;
    std::set<RefPtr<RefObject>> m_trackedObjects;
};

} // namespace rhi
