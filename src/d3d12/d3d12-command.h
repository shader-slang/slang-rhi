#pragma once

#include "d3d12-base.h"
#include "d3d12-shader-object.h"
#include "d3d12-constant-buffer-pool.h"

#include <deque>
#include <list>

namespace rhi::d3d12 {

class CommandQueueImpl : public CommandQueue
{

public:
    ComPtr<ID3D12Device> m_d3dDevice;
    ComPtr<ID3D12CommandQueue> m_d3dQueue;
    ComPtr<ID3D12Fence> m_trackingFence;
    HANDLE m_globalWaitHandle;
    uint32_t m_queueIndex = 0;

    uint64_t m_lastSubmittedID = 0;
    uint64_t m_lastFinishedID = 0;

    std::mutex m_mutex;
    std::list<RefPtr<CommandBufferImpl>> m_commandBuffersPool;
    std::list<RefPtr<CommandBufferImpl>> m_commandBuffersInFlight;

    // Deferred delete queue for GPU resources.
    // Resources are held here until the GPU has finished using them.
    struct DeferredDelete
    {
        uint64_t submissionID;
        Resource* resource;
    };
    std::mutex m_deferredDeletesMutex;
    std::list<DeferredDelete> m_deferredDeletes;

#if SLANG_RHI_ENABLE_AFTERMATH
    GFSDK_Aftermath_ContextHandle m_aftermathContext;
#endif

    CommandQueueImpl(Device* device, QueueType type);
    ~CommandQueueImpl();

    Result init(uint32_t queueIndex);
    void shutdown();

    Result createCommandBuffer(CommandBufferImpl** outCommandBuffer);
    Result getOrCreateCommandBuffer(CommandBufferImpl** outCommandBuffer);
    void retireCommandBuffer(CommandBufferImpl* commandBuffer);
    void retireCommandBuffers();
    uint64_t updateLastFinishedID();

    /// Queue a resource for deferred deletion. The resource will be deleted
    /// once the GPU has finished all work submitted up to this point.
    void deferDelete(Resource* resource);

    /// Delete deferred resources that are no longer in use by the GPU.
    void executeDeferredDeletes();

    // ICommandQueue implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL createCommandEncoder(ICommandEncoder** outEncoder) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL submit(const SubmitDesc& desc) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL waitOnHost() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

class CommandEncoderImpl : public CommandEncoder
{
public:
    CommandQueueImpl* m_queue;
    RefPtr<CommandBufferImpl> m_commandBuffer;

    CommandEncoderImpl(Device* device, CommandQueueImpl* queue);
    ~CommandEncoderImpl();

    Result init();

    virtual Result getBindingData(RootShaderObject* rootObject, BindingData*& outBindingData) override;

    // ICommandEncoder implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL finish(ICommandBuffer** outCommandBuffer) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

class CommandBufferImpl : public CommandBuffer
{
public:
    CommandQueueImpl* m_queue;
    ComPtr<ID3D12CommandAllocator> m_d3dCommandAllocator;
    ComPtr<ID3D12GraphicsCommandList> m_d3dCommandList;
    GPUDescriptorArena m_cbvSrvUavArena;
    GPUDescriptorArena m_samplerArena;
    ConstantBufferPool m_constantBufferPool;
    BindingCache m_bindingCache;
    uint64_t m_submissionID = 0;

#if SLANG_RHI_ENABLE_AFTERMATH
    GFSDK_Aftermath_ContextHandle m_aftermathContext;
    AftermathMarkerTracker m_aftermathMarkerTracker;
#endif

    CommandBufferImpl(Device* device, CommandQueueImpl* queue);
    ~CommandBufferImpl();

    Result init();
    virtual Result reset() override;

    // ICommandBuffer implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

} // namespace rhi::d3d12
