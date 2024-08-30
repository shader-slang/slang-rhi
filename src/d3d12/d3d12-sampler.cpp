#include "d3d12-sampler.h"

namespace rhi::d3d12 {

SamplerImpl::~SamplerImpl()
{
    m_allocator->free(m_descriptor);
}

Result SamplerImpl::getNativeHandle(InteropHandle* outHandle)
{
    outHandle->api = InteropHandleAPI::D3D12CpuDescriptorHandle;
    outHandle->handleValue = m_descriptor.cpuHandle.ptr;
    return SLANG_OK;
}

} // namespace rhi::d3d12
