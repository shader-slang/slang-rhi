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

    // TODO we should rename this
    virtual void comFree() override { m_device.breakStrongReference(); }

protected:
    BreakableReference<Device> m_device;
};

} // namespace rhi
