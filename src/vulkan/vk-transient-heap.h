#pragma once

#include "vk-base.h"
#include "vk-buffer.h"
#include "vk-command-buffer.h"

#include <vector>

namespace rhi::vk {

class TransientResourceHeapImpl : public TransientResourceHeapBaseImpl<DeviceImpl, BufferImpl>
{
private:
    typedef TransientResourceHeapBaseImpl<DeviceImpl, BufferImpl> Super;

public:
    VkCommandPool m_commandPool;
    DescriptorSetAllocator m_descSetAllocator;
    std::vector<VkFence> m_fences;
    Index m_fenceIndex = -1;
    std::vector<RefPtr<CommandBufferImpl>> m_commandBufferPool;
    uint32_t m_commandBufferAllocId = 0;
    VkFence getCurrentFence() { return m_fences[m_fenceIndex]; }
    void advanceFence();

    Result init(const ITransientResourceHeap::Desc& desc, DeviceImpl* device);
    ~TransientResourceHeapImpl();

public:
    virtual SLANG_NO_THROW Result SLANG_MCALL createCommandBuffer(ICommandBuffer** outCommandBuffer) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL synchronizeAndReset() override;
};

} // namespace rhi::vk
