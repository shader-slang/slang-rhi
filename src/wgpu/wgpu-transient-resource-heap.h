#pragma once

#include "wgpu-base.h"
#include "wgpu-buffer.h"
#include "../transient-resource-heap-base.h"

namespace rhi::wgpu {

class TransientResourceHeapImpl : public TransientResourceHeapBaseImpl<DeviceImpl, BufferImpl>
{
private:
    typedef TransientResourceHeapBaseImpl<DeviceImpl, BufferImpl> Super;

public:
    Result init(const ITransientResourceHeap::Desc& desc, DeviceImpl* device);
    ~TransientResourceHeapImpl();

public:
#if 0
    virtual SLANG_NO_THROW Result SLANG_MCALL createCommandBuffer(ICommandBuffer** outCommandBuffer) override;
#endif
    virtual SLANG_NO_THROW Result SLANG_MCALL synchronizeAndReset() override;
};

} // namespace rhi::wgpu
