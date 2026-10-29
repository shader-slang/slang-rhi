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

void AccelerationStructureImpl::deleteThis()
{
    getDevice<DeviceImpl>()->deferDelete(this);
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
    *outHandle = DescriptorHandle{DescriptorHandleType::AccelerationStructure, (uint64_t)m_handle};
    return SLANG_OK;
}

MicromapImpl::MicromapImpl(Device* device, const MicromapDesc& desc)
    : Micromap(device, desc)
{
}

MicromapImpl::~MicromapImpl()
{
    if (m_buffer)
        SLANG_CUDA_ASSERT_ON_FAIL(cuMemFree(m_buffer));
}

void MicromapImpl::deleteThis()
{
    getDevice<DeviceImpl>()->deferDelete(this);
}

Result MicromapImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::CUdeviceptr;
    outHandle->value = m_buffer;
    return SLANG_OK;
}

DeviceAddress MicromapImpl::getDeviceAddress()
{
    return m_buffer;
}

} // namespace rhi::cuda
