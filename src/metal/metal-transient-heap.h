#pragma once

#include "metal-base.h"
#include "metal-buffer.h"
#include "metal-command-buffer.h"

namespace rhi::metal {

class TransientResourceHeapImpl : public TransientResourceHeapBaseImpl<DeviceImpl, BufferImpl>
{
private:
    typedef TransientResourceHeapBaseImpl<DeviceImpl, BufferImpl> Super;

public:
    NS::SharedPtr<MTL::CommandQueue> m_commandQueue;

    Result init(const ITransientResourceHeap::Desc& desc, DeviceImpl* device);
    ~TransientResourceHeapImpl();

public:
    virtual SLANG_NO_THROW Result SLANG_MCALL createCommandBuffer(ICommandBuffer** outCommandBuffer) override;
    virtual SLANG_NO_THROW Result SLANG_MCALL synchronizeAndReset() override;
};

} // namespace rhi::metal
