#include "d3d12-sampler.h"
#include "d3d12-device.h"

namespace rhi::d3d12 {

SamplerImpl::~SamplerImpl()
{
    m_device->m_cpuSamplerHeap->free(m_descriptor);
}

Result SamplerImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::D3D12CpuDescriptorHandle;
    outHandle->value = m_descriptor.cpuHandle.ptr;
    return SLANG_OK;
}

} // namespace rhi::d3d12
