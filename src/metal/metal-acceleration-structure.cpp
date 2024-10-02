#include "metal-acceleration-structure.h"
#include "metal-device.h"

namespace rhi::metal {

AccelerationStructureImpl::~AccelerationStructureImpl()
{
    m_device->m_accelerationStructures.freeList.push_back(m_globalIndex);
    m_device->m_accelerationStructures.list[m_globalIndex] = nullptr;
    m_device->m_accelerationStructures.dirty = true;
}

Result AccelerationStructureImpl::getNativeHandle(NativeHandle* outHandle)
{
    outHandle->type = NativeHandleType::MTLAccelerationStructure;
    outHandle->value = (uint64_t)m_accelerationStructure.get();
    return SLANG_OK;
}

AccelerationStructureHandle AccelerationStructureImpl::getHandle()
{
    return AccelerationStructureHandle{m_globalIndex};
}

DeviceAddress AccelerationStructureImpl::getDeviceAddress()
{
    return 0;
}

} // namespace rhi::metal
