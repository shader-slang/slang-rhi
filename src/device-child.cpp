#include "device-child.h"
#include "device.h"

namespace rhi {

DeviceChild::DeviceChild(Device* device)
    : m_device(device)
{
}

DeviceChild::~DeviceChild() {}

void DeviceChild::breakStrongReferenceToDevice()
{
    m_device.breakStrongReference();
}

void DeviceChild::establishStrongReferenceToDevice()
{
    m_device.establishStrongReference();
}

} // namespace rhi
