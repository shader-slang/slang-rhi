#pragma once

#include "reference.h"
#include "rhi-shared-fwd.h"

namespace rhi {

class DeviceChild : public ComObject
{
public:
    DeviceChild(Device* device)
        : m_device(device)
    {
    }

    template<typename T = Device>
    T* getDevice()
    {
        return static_cast<T*>(m_device.get());
    }

    void breakStrongReferenceToDevice() { m_device.breakStrongReference(); }

    void establishStrongReferenceToDevice() { m_device.establishStrongReference(); }

protected:
    BreakableReference<Device> m_device;
};

} // namespace rhi
