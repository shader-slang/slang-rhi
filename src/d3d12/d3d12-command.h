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

    CommandQueueImpl(Device* device, QueueType type);
    ~CommandQueueImpl();

    Result init(uint32_t queueIndex);

    Result createCommandBuffer(CommandBufferImpl** outCommandBuffer);
    Result getOrCreateCommandBuffer(CommandBufferImpl** outCommandBuffer);
    void retireCommandBuffer(CommandBufferImpl* commandBuffer);
    void retireCommandBuffers();
    uint64_t updateLastFinishedID();

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

    CommandBufferImpl(Device* device, CommandQueueImpl* queue);
    ~CommandBufferImpl();

    Result init();
    virtual Result reset() override;

    // ICommandBuffer implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

} // namespace rhi::d3d12
