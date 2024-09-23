#include "wgpu-transient-resource-heap.h"
#include "wgpu-device.h"
#include "wgpu-command-buffer.h"
#include "wgpu-pipeline.h"

namespace rhi::wgpu {

Result TransientResourceHeapImpl::init(const ITransientResourceHeap::Desc& desc, DeviceImpl* device)
{
    Super::init(
        desc,
        256, // TODO
        device
    );

    return SLANG_OK;
}

TransientResourceHeapImpl::~TransientResourceHeapImpl() {}

Result TransientResourceHeapImpl::createCommandBuffer(ICommandBuffer** outCmdBuffer)
{
    RefPtr<CommandBufferImpl> cmdBuffer = new CommandBufferImpl();
    cmdBuffer->m_device = m_device.get();

    WGPUCommandEncoderDescriptor desc = {};
    desc.label = nullptr; // TODO assign label
    cmdBuffer->m_commandEncoder = m_device->m_ctx.api.wgpuDeviceCreateCommandEncoder(m_device->m_ctx.device, nullptr);
    if (!cmdBuffer->m_commandEncoder)
    {
        return SLANG_FAIL;
    }
    cmdBuffer->m_transientHeap = this;
    returnComPtr(outCmdBuffer, cmdBuffer);
    return SLANG_OK;
}

Result TransientResourceHeapImpl::synchronizeAndReset()
{
    Super::reset();
    return SLANG_OK;
}

Result DeviceImpl::createTransientResourceHeap(
    const ITransientResourceHeap::Desc& desc,
    ITransientResourceHeap** outHeap
)
{
    RefPtr<TransientResourceHeapImpl> heap = new TransientResourceHeapImpl();
    SLANG_RETURN_ON_FAIL(heap->init(desc, this));
    returnComPtr(outHeap, heap);
    return SLANG_OK;
}

} // namespace rhi::wgpu
