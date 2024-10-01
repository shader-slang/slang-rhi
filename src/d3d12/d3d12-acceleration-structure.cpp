#include "d3d12-acceleration-structure.h"
#include "d3d12-buffer.h"
#include "d3d12-device.h"

namespace rhi::d3d12 {

#if SLANG_RHI_DXR

AccelerationStructureImpl::~AccelerationStructureImpl()
{
    if (m_descriptor)
        m_device->m_cpuViewHeap->free(m_descriptor);
}

Result AccelerationStructureImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::D3D12DeviceAddress;
    outHandle->value = getDeviceAddress();
    return SLANG_OK;
}

AccelerationStructureHandle AccelerationStructureImpl::getHandle()
{
    return AccelerationStructureHandle{m_buffer->getDeviceAddress()};
}

DeviceAddress AccelerationStructureImpl::getDeviceAddress()
{
    return m_buffer->getDeviceAddress();
}

#endif // SLANG_RHI_DXR

} // namespace rhi::d3d12
