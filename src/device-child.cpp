#include "device-child.h"
#include "device.h"

namespace rhi {

DeviceChild::DeviceChild(Device* device)
    : m_device(device)
{
}

DeviceChild::~DeviceChild() {}

RefObject* DeviceChild::getLifetimeOwner() const noexcept
{
    return m_device;
}

} // namespace rhi
