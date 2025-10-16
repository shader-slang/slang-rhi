#include "cuda-acceleration-structure.h"
#include "cuda-device.h"
#include "cuda-utils.h"

namespace rhi::cuda {

AccelerationStructureImpl::AccelerationStructureImpl(Device* device, const AccelerationStructureDesc& desc)
    : AccelerationStructure(device, desc)
{
}

AccelerationStructureImpl::~AccelerationStructureImpl()
{
    SLANG_CUDA_ASSERT_ON_FAIL(cuMemFree(m_buffer));
    SLANG_CUDA_ASSERT_ON_FAIL(cuMemFree(m_propertyBuffer));
}

Result AccelerationStructureImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::OptixTraversableHandle;
    outHandle->value = (uint64_t)m_handle;
    return SLANG_OK;
}

AccelerationStructureHandle AccelerationStructureImpl::getHandle()
{
    return AccelerationStructureHandle{m_handle};
}

DeviceAddress AccelerationStructureImpl::getDeviceAddress()
{
    return m_buffer;
}

Result AccelerationStructureImpl::getDescriptorHandle(DescriptorHandle* outHandle)
{
    outHandle->type = DescriptorHandleType::AccelerationStructure;
    outHandle->value = (uint64_t)m_handle;
    return SLANG_OK;
}

} // namespace rhi::cuda
