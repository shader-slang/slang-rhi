#pragma once

#include <slang-rhi.h>

#include "slang-context.h"

#include "core/common.h"
#include "core/arena-allocator.h"

#include "reference.h"
#include "command-list.h"

#include "rhi-shared-fwd.h"

#include <set>

namespace rhi {

struct BindingData
{};

template<typename TDevice>
class CommandQueue : public ICommandQueue, public ComObject
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL
    ICommandQueue* getInterface(const Guid& guid)
    {
        if (guid == ISlangUnknown::getTypeGuid() || guid == ICommandQueue::getTypeGuid())
            return static_cast<ICommandQueue*>(this);
        return nullptr;
    }

    virtual void comFree() override { breakStrongReferenceToDevice(); }

public:
    CommandQueue(TDevice* device, QueueType type)
    {
        m_device.setWeakReference(device);
        m_type = type;
    }

    void breakStrongReferenceToDevice() { m_device.breakStrongReference(); }
    void establishStrongReferenceToDevice() { m_device.establishStrongReference(); }

    // ICommandQueue implementation
    virtual SLANG_NO_THROW QueueType SLANG_MCALL getType() override { return m_type; }

public:
    BreakableReference<TDevice> m_device;
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

    // IPassEncoder implementation
    virtual SLANG_NO_THROW void SLANG_MCALL pushDebugGroup(const char* name, float rgbColor[3]) override;
    virtual SLANG_NO_THROW void SLANG_MCALL popDebugGroup() override;
    virtual SLANG_NO_THROW void SLANG_MCALL insertDebugMarker(const char* name, float rgbColor[3]) override;

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
    virtual SLANG_NO_THROW void SLANG_MCALL
    bindPipeline(IComputePipeline* pipeline, IShaderObject* rootObject) override;
    virtual SLANG_NO_THROW void SLANG_MCALL dispatchCompute(uint32_t x, uint32_t y, uint32_t z) override;
    virtual SLANG_NO_THROW void SLANG_MCALL dispatchComputeIndirect(IBuffer* argBuffer, uint64_t offset) override;

    // IPassEncoder implementation
    virtual SLANG_NO_THROW void SLANG_MCALL pushDebugGroup(const char* name, float rgbColor[3]) override;
    virtual SLANG_NO_THROW void SLANG_MCALL popDebugGroup() override;
    virtual SLANG_NO_THROW void SLANG_MCALL insertDebugMarker(const char* name, float rgbColor[3]) override;

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
    virtual SLANG_NO_THROW IShaderObject* SLANG_MCALL
    bindPipeline(IRayTracingPipeline* pipeline, IShaderTable* shaderTable) override;
    virtual SLANG_NO_THROW void SLANG_MCALL
    bindPipeline(IRayTracingPipeline* pipeline, IShaderTable* shaderTable, IShaderObject* rootObject) override;
    virtual SLANG_NO_THROW void SLANG_MCALL
    dispatchRays(uint32_t rayGenShaderIndex, uint32_t width, uint32_t height, uint32_t depth) override;

    // IPassEncoder implementation
    virtual SLANG_NO_THROW void SLANG_MCALL pushDebugGroup(const char* name, float rgbColor[3]) override;
    virtual SLANG_NO_THROW void SLANG_MCALL popDebugGroup() override;
    virtual SLANG_NO_THROW void SLANG_MCALL insertDebugMarker(const char* name, float rgbColor[3]) override;

    virtual SLANG_NO_THROW void SLANG_MCALL end() override;
};

class CommandEncoder : public ICommandEncoder, public ComObject
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

    CommandEncoder();

    virtual Device* getDevice() = 0;
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
        uint32_t subresourceDataCount
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
    resolveQuery(IQueryPool* queryPool, uint32_t index, uint32_t count, IBuffer* buffer, uint64_t offset) override;

    virtual SLANG_NO_THROW void SLANG_MCALL buildAccelerationStructure(
        const AccelerationStructureBuildDesc& desc,
        IAccelerationStructure* dst,
        IAccelerationStructure* src,
        BufferOffsetPair scratchBuffer,
        uint32_t propertyQueryCount,
        AccelerationStructureQueryDesc* queryDescs
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
        AccelerationStructureQueryDesc* queryDescs
    ) override;

    virtual SLANG_NO_THROW void SLANG_MCALL
    serializeAccelerationStructure(BufferOffsetPair dst, IAccelerationStructure* src) override;

    virtual SLANG_NO_THROW void SLANG_MCALL
    deserializeAccelerationStructure(IAccelerationStructure* dst, BufferOffsetPair src) override;

    virtual SLANG_NO_THROW void SLANG_MCALL
    convertCooperativeVectorMatrix(const ConvertCooperativeVectorMatrixDesc* descs, uint32_t descCount) override;

    virtual SLANG_NO_THROW void SLANG_MCALL setBufferState(IBuffer* buffer, ResourceState state) override;

    virtual SLANG_NO_THROW void SLANG_MCALL
    setTextureState(ITexture* texture, SubresourceRange subresourceRange, ResourceState state) override;

    virtual SLANG_NO_THROW void SLANG_MCALL pushDebugGroup(const char* name, float rgbColor[3]) override;
    virtual SLANG_NO_THROW void SLANG_MCALL popDebugGroup() override;
    virtual SLANG_NO_THROW void SLANG_MCALL insertDebugMarker(const char* name, float rgbColor[3]) override;

    virtual SLANG_NO_THROW void SLANG_MCALL writeTimestamp(IQueryPool* queryPool, uint32_t queryIndex) override;

    virtual SLANG_NO_THROW Result SLANG_MCALL finish(ICommandBuffer** outCommandBuffer) override;
};

class CommandBuffer : public ICommandBuffer, public ComObject
{
public:
    SLANG_COM_OBJECT_IUNKNOWN_ALL
    ICommandBuffer* getInterface(const Guid& guid);

public:
    CommandBuffer()
        : m_commandList(m_allocator, m_trackedObjects)
    {
    }
    virtual ~CommandBuffer() = default;

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
