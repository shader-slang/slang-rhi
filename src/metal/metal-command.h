#pragma once

#include "metal-base.h"
#include "metal-shader-object.h"

#include "core/ring-queue.h"

namespace rhi::metal {

/// Metal backend synchronization model
///
/// Key differences from Vulkan/D3D12 that motivate this design:
///
///   Metal has no pipeline barriers or resource state transitions. In Vulkan
///   you insert vkCmdPipelineBarrier with src/dst stage masks and memory
///   barriers; in D3D12 you call ResourceBarrier to transition resource
///   states. Metal has none of this. Instead, Metal offers two mechanisms:
///
///     - "Hazard tracking" (MTLHazardTrackingModeTracked): the driver
///       automatically tracks resource access and inserts barriers. This is
///       the default, similar to D3D11's implicit model. It has overhead and
///       cannot track resources accessed via GPU pointers in argument buffers.
///
///     - Manual synchronization with MTL::Fence / MTL::SharedEvent and
///       intra-pass memory barriers (memoryBarrier). This is what we use.
///
///   Metal command buffers contain "encoders" (passes), not inline commands.
///   A Metal encoder is roughly analogous to a Vulkan render/compute pass:
///   you create an encoder, record commands into it, then end it. You cannot
///   interleave commands from different encoder types (unlike Vulkan where
///   barriers, copies, and dispatches can appear in any order within a CB).
///   Switching encoder type requires ending the current encoder and creating
///   a new one, which is where inter-encoder synchronization is needed.
///
///   Metal has no equivalent to Vulkan's subpass dependencies or D3D12's
///   split barriers. Synchronization between encoders (passes) is achieved
///   with MTL::Fence, which provides both execution ordering AND memory
///   visibility (cache flush/invalidate) for untracked resources. Within a
///   single encoder, intra-pass barriers (memoryBarrier) serve a similar
///   role to Vulkan's vkCmdPipelineBarrier.
///
/// Why all resources are untracked:
///
///   This backend uses MTLHazardTrackingModeUntracked on all resources
///   because the rhi API exposes GPU addresses (device pointers) that shaders
///   access through argument buffers. Metal's automatic hazard tracking
///   cannot see these pointer-based accesses -- it only tracks resources
///   explicitly bound to encoder slots. Using tracked mode would give a
///   false sense of safety while missing the accesses that matter most.
///
/// Synchronization mechanisms:
///
///   m_queueFence (MTL::Fence) -- GPU-side memory visibility
///     The closest Vulkan analogy would be a full pipeline barrier between
///     every render/compute pass, but implemented as a single reusable fence
///     rather than per-resource barriers with stage masks.
///
///     Every Metal encoder calls waitForFence(m_queueFence) at creation and
///     updateFence(m_queueFence) at end. This serializes all encoder
///     execution with full memory visibility, both within a single
///     MTL::CommandBuffer and across command buffers on the same queue.
///
///     MTL::Fence is a Metal-specific primitive with no direct Vulkan/D3D12
///     equivalent. Unlike VkFence (which is GPU->CPU only), MTL::Fence is
///     GPU->GPU and operates between encoders/passes. Unlike VkEvent,
///     MTL::Fence does not bake a value at recording time -- waitForFence
///     resolves at execution time against the most recent updateFence. This
///     means recording order is irrelevant; only commit order matters.
///
///     The first encoder in the first-ever command buffer waits on a
///     never-updated fence, which completes immediately.
///
///   m_trackingEvent (MTL::SharedEvent) -- CPU-side tracking
///     Analogous to a VkFence/ID3D12Fence signaled at the end of each
///     submit. A monotonic counter signaled on the last command buffer of
///     each submit() call. Used solely for:
///       - Deferred deletion: resources are tagged with the submission ID
///         and freed once signaledValue() passes that ID.
///       - waitOnHost(): blocks CPU until all submitted work completes.
///     This event does NOT participate in GPU-side memory visibility.
///
/// Device-level operations (readBuffer, createBuffer with init data,
/// createTexture with init data) create ad-hoc blit encoders that participate
/// in the fence chain via waitForFence/updateFence on m_queueFence, then call
/// waitUntilCompleted() for CPU-side visibility (on Apple Silicon UMA,
/// waitUntilCompleted is sufficient -- no explicit cache flush needed).
///
class CommandQueueImpl : public CommandQueue
{
public:
    NS::SharedPtr<MTL::CommandQueue> m_commandQueue;

    /// Single fence shared across all command buffers on this queue.
    /// Provides inter-encoder and inter-command-buffer memory visibility
    /// for untracked resources. See synchronization model above.
    NS::SharedPtr<MTL::Fence> m_queueFence;

    /// Tracking event for deferred deletion and CPU-side waiting.
    /// Signaled on the last CB of each submit() with m_lastSubmittedID.
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
    virtual SLANG_NO_THROW Result SLANG_MCALL createCommandEncoder(
        const CommandEncoderDesc& desc,
        ICommandEncoder** outEncoder
    ) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL submit(const SubmitDesc& desc) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL waitOnHost() override;
    virtual SLANG_NO_THROW Result SLANG_MCALL getNativeHandle(NativeHandle* outHandle) override;
};

class CommandEncoderImpl : public CommandEncoder
{
public:
    CommandQueueImpl* m_queue;
    RefPtr<CommandBufferImpl> m_commandBuffer;

    CommandEncoderImpl(Device* device, CommandQueueImpl* queue, const CommandEncoderDesc& desc);
    ~CommandEncoderImpl();

    Result init();

    virtual Result getBindingData(RootShaderObject* rootObject, BindingData*& outBindingData) override;

    // ICommandEncoder implementation
    virtual SLANG_NO_THROW Result SLANG_MCALL finish(
        const CommandBufferDesc& desc,
        ICommandBuffer** outCommandBuffer
    ) override;
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
