#pragma once

#include "metal-base.h"
#include "metal-shader-object.h"

#include "core/ring-queue.h"

namespace rhi::metal {

class CommandQueueImpl : public CommandQueue
{
public:
    NS::SharedPtr<MTL::CommandQueue> m_commandQueue;
    NS::SharedPtr<MTL::SharedEvent> m_trackingEvent;
    NS::SharedPtr<MTL::SharedEventListener> m_trackingEventListener;
    uint64_t m_lastSubmittedID;
    uint64_t m_lastFinishedID;
    std::list<RefPtr<CommandBufferImpl>> m_commandBuffersInFlight;

    // Deferred delete queue for GPU resources.
    // Resources are held here until the GPU has finished using them.
    struct DeferredDelete
    {
        uint64_t submissionID;
        Resource* resource;
    };
    std::mutex m_deferredDeleteQueueMutex;
    RingQueue<DeferredDelete> m_deferredDeleteQueue;

    CommandQueueImpl(Device* device, QueueType type);
    ~CommandQueueImpl();

    void init(NS::SharedPtr<MTL::CommandQueue> commandQueue);
    void shutdown();

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
    NS::SharedPtr<MTL::CommandBuffer> m_commandBuffer;
    BindingCache m_bindingCache;
    uint64_t m_submissionID;

    CommandBufferImpl(Device* device, CommandQueueImpl* queue);
    ~CommandBufferImpl();

    Result init();
    virtual Result reset() override;

    // ICommandBuffer implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

} // namespace rhi::metal
