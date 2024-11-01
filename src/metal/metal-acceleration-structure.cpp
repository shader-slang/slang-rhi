#include "metal-acceleration-structure.h"
#include "metal-device.h"
#include "metal-util.h"

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

Result DeviceImpl::createAccelerationStructure(
    const AccelerationStructureDesc& desc,
    IAccelerationStructure** outAccelerationStructure
)
{
    AUTORELEASEPOOL

    RefPtr<AccelerationStructureImpl> result = new AccelerationStructureImpl(this, desc);
    result->m_accelerationStructure = NS::TransferPtr(m_device->newAccelerationStructure(desc.size));

    uint32_t globalIndex = 0;
    if (!m_accelerationStructures.freeList.empty())
    {
        globalIndex = m_accelerationStructures.freeList.back();
        m_accelerationStructures.freeList.pop_back();
        m_accelerationStructures.list[globalIndex] = result->m_accelerationStructure.get();
    }
    else
    {
        globalIndex = m_accelerationStructures.list.size();
        m_accelerationStructures.list.push_back(result->m_accelerationStructure.get());
    }
    m_accelerationStructures.dirty = true;
    result->m_globalIndex = globalIndex;

    returnComPtr(outAccelerationStructure, result);
    return SLANG_OK;
}

} // namespace rhi::metal
