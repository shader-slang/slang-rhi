#include "d3d12-command-buffer.h"
#include "d3d12-transient-heap.h"

namespace rhi::d3d12 {

ICommandBuffer* CommandBufferImpl::getInterface(const Guid& guid)
{
    if (guid == GUID::IID_ISlangUnknown || guid == GUID::IID_ICommandBuffer)
        return static_cast<ICommandBuffer*>(this);
    return nullptr;
}

Result CommandBufferImpl::getNativeHandle(NativeHandle* handle)
{
    handle->type = NativeHandleType::D3D12GraphicsCommandList;
    handle->value = (uint64_t)m_cmdList.get();
    return SLANG_OK;
}

} // namespace rhi::d3d12
